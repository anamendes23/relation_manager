/**
 * @file SQLExec.cpp - implementation of SQLExec class
 * @author Kevin Lundeen
 * @author Ana Mendes
 * @author Erika Skornia-Olsen
 * @see "Seattle University, CPSC5300, Spring 2022"
 */
#include "SQLExec.h"
#include "EvalPlan.h"

using namespace std;
using namespace hsql;

// define static data
Tables *SQLExec::tables = nullptr;
Indices *SQLExec::indices = nullptr;

// make query result be printable
ostream &operator<<(ostream &out, const QueryResult &qres) {
    if (qres.column_names != nullptr) {
        for (auto const &column_name: *qres.column_names)
            out << column_name << " ";
        out << endl << "+";
        for (unsigned int i = 0; i < qres.column_names->size(); i++)
            out << "----------+";
        out << endl;
        for (auto const &row: *qres.rows) {
            for (auto const &column_name: *qres.column_names) {
                Value value = row->at(column_name);
                switch (value.data_type) {
                    case ColumnAttribute::INT:
                        out << value.n;
                        break;
                    case ColumnAttribute::TEXT:
                        out << "\"" << value.s << "\"";
                        break;
                    case ColumnAttribute::BOOLEAN:
                        out << (value.n == 0 ? "false" : "true");
                        break;
                    default:
                        out << "???";
                }
                out << " ";
            }
            out << endl;
        }
    }
    out << qres.message;
    return out;
}

QueryResult::~QueryResult() {
    if (column_names != nullptr)
        delete column_names;
    if (column_attributes != nullptr)
        delete column_attributes;
    if (rows != nullptr) {
        for (auto row: *rows)
            delete row;
        delete rows;
    }
}


QueryResult *SQLExec::execute(const SQLStatement *statement) {
    // initialize _tables table, if not yet present
    if (SQLExec::tables == nullptr) {
        SQLExec::tables = new Tables();
        SQLExec::indices = new Indices();
    }

    try {
        switch (statement->type()) {
            case kStmtCreate:
                return create((const CreateStatement *) statement);
            case kStmtDrop:
                return drop((const DropStatement *) statement);
            case kStmtShow:
                return show((const ShowStatement *) statement);
            case kStmtInsert:
                return insert((const InsertStatement *) statement);
            case kStmtDelete:
                return del((const DeleteStatement *) statement);
            case kStmtSelect:
                return select((const SelectStatement *) statement);
            default:
                return new QueryResult("not implemented");
        }
    } catch (DbRelationError &e) {
        throw SQLExecError(string("DbRelationError: ") + e.what());
    }
}

ColumnAttribute get_column_type(string column, ColumnNames columns, ColumnAttributes column_types) {
    for(uint i = 0; i < columns.size(); i++) {
        if(columns[i] == column) {
            return column_types[i];
        }
    }

    throw SQLExecError("unkown column " + column);
}

QueryResult *SQLExec::insert(const InsertStatement *statement) {
    Identifier table_name = statement->tableName;
    std::vector<char*> insert_columns = *statement->columns;
    std::vector<Expr*> insert_values = *statement->values;
    DbRelation &table = SQLExec::tables->get_table(table_name);
    // add values to row to be inserted
    ValueDict row;
    ColumnNames columns = table.get_column_names();
    ColumnAttributes column_types = table.get_column_attributes();
    for(uint i = 0; i < insert_values.size(); i++) {
        // get column by name and check it's info, such as type
        Identifier column = insert_columns[i];
        ColumnAttribute column_type = get_column_type(column, columns, column_types);

        switch(column_type.get_data_type()) {
            case ColumnAttribute::INT:
                row[column] = Value(insert_values[i]->ival);
                break;
            case ColumnAttribute::TEXT:
                row[column] = Value(insert_values[i]->name);
                break;
            default:
                throw SQLExecError("don't know how to handle data type in INSERT");
        }
    }
    // insert into table
    Handle insert_handle = table.insert(&row);

    // update index
    IndexNames index_names = SQLExec::indices->get_index_names(table_name);
    // for each index name in that table, get the index and insert the row handle
    for(auto const& index_name : index_names) {
        DbIndex &index = SQLExec::indices->get_index(table_name, index_name);
        index.insert(insert_handle);
    }
    string suffix = "";
    if(index_names.size() > 0) {
        suffix = " and from " + to_string(index_names.size()) + " indices";
    }

    /*
        For insert, your job is to construct the ValueDict row to insert and insert it.
        Also, make sure to add to any indices. Useful methods here are get_table,
        get_index_names, get_index. Make sure you account for the fact that the user has
        the ability to list column names in a different order than the table definition. 
    */
    return new QueryResult("successfully inserted 1 row into " + table_name + suffix);
}

// Pull out conjunctions of equality predicates from parse tree
ValueDict* get_where_conjunction(const Expr *expr) {
    ValueDict* where = new ValueDict();
    // check if expr is invalid
    if (expr->type != kExprOperator)
        throw DbRelationError("Invalid statement");

    // find conjunction AND
    if (expr->opType == Expr::AND) {
        // get expression before AND
        // get expression after AND
        // place both in where
        ValueDict* first = get_where_conjunction(expr->expr);
        ValueDict* second = get_where_conjunction(expr->expr2);
        where->insert(first->begin(), first->end());
        where->insert(second->begin(), second->end());
        delete first;
        delete second;
    // find operator: =
    } else if (expr->opChar == '=') {
        // put get_value_from_parse values in where
        string index = expr->expr->name;
        // for int, place int in where
        // for string, place string in where
        if (expr->expr2->type == kExprLiteralInt) {
          (*where)[index] = Value(int32_t(expr->expr2->ival));
        } else if (expr->expr2->type == kExprLiteralString) {
            (*where)[index] = Value(expr->expr2->name);
        } else {
            throw DbRelationError("Don't know how to handle " + expr->expr2->type);
        }
    }  
    return where;
}

QueryResult *SQLExec::del(const DeleteStatement *statement) {
    // get table name
    Identifier tableName = statement->tableName;

    // get table
    DbRelation &table = SQLExec::tables->get_table(tableName);

    // create evaluation plan and execute
    EvalPlan *plan = new EvalPlan(table);
    if (statement->expr != nullptr)
        plan = new EvalPlan(get_where_conjunction(statement->expr), plan);
    EvalPlan *optimized = plan->optimize();
    EvalPipeline pipeline = optimized->pipeline();

    // delete all the handles
    IndexNames index_names = SQLExec::indices->get_index_names(tableName);
    Handles *handles = pipeline.second;
    uint rows = 0;
    uint indices = 0;
    for (auto const& handle: *handles) {
        for (auto const& index_name: index_names) {
            DbIndex &index = SQLExec::indices->get_index(tableName, index_name);
            index.del(handle);
            indices++;
        }
        table.del(handle);
        rows++;
    }
    return new QueryResult("successfully deleted " + to_string(rows) + " rows from " + tableName + " " + to_string(indices) + " indices");
}

QueryResult *SQLExec::select(const SelectStatement *statement) {
    // SELECT should translate into an evaluation plan with a project plan on a select plan.
    // The enclosed select plan should be annotated with a table scan.
    // get table name
    Identifier tableName = statement->fromTable->getName();

    // get table
    DbRelation &table = SQLExec::tables->get_table(tableName);

    // start base of plan at tablescan
    EvalPlan *plan = new EvalPlan(table);

    // enclose in select if a where clause
    if (statement->whereClause != nullptr)
        plan = new EvalPlan(get_where_conjunction(statement->whereClause), plan);

    // column names to return at end
    ColumnNames *column_names = new ColumnNames;
    // column attributes to return at end
    ColumnAttributes *column_attributes = table.get_column_attributes(*column_names);

    for (auto const& stmt: *statement->selectList) {
        if (stmt->type == kExprStar) {
            // if returning all data from table, find all columns
            ColumnNames columnNames = table.get_column_names();
            // iterate through column names, push into column names vector
            for (auto const &col: columnNames)
                column_names->push_back(col);
        } else {
        // if returning from some columns, find those columns
        // push back
            column_names->push_back(stmt->name);
        }
    }
    plan = new EvalPlan(column_names, plan);

    // optimize plan and evaluate optimized plan
    EvalPlan *optimized = plan->optimize();
    ValueDicts *rows = optimized->evaluate();

    return new QueryResult(column_names, column_attributes, rows, "successfully return " + to_string(rows->size()) + " rows");
}

void
SQLExec::column_definition(const ColumnDefinition *col, Identifier &column_name, ColumnAttribute &column_attribute) {
    column_name = col->name;
    switch (col->type) {
        case ColumnDefinition::INT:
            column_attribute.set_data_type(ColumnAttribute::INT);
            break;
        case ColumnDefinition::TEXT:
            column_attribute.set_data_type(ColumnAttribute::TEXT);
            break;
        case ColumnDefinition::DOUBLE:
        default:
            throw SQLExecError("unrecognized data type");
    }
}

// CREATE ...
QueryResult *SQLExec::create(const CreateStatement *statement) {
    switch (statement->type) {
        case CreateStatement::kTable:
            return create_table(statement);
        case CreateStatement::kIndex:
            return create_index(statement);
        default:
            return new QueryResult("Only CREATE TABLE and CREATE INDEX are implemented");
    }
}

QueryResult *SQLExec::create_table(const CreateStatement *statement) {
    Identifier table_name = statement->tableName;
    ColumnNames column_names;
    ColumnAttributes column_attributes;
    Identifier column_name;
    ColumnAttribute column_attribute;
    for (ColumnDefinition *col : *statement->columns) {
        column_definition(col, column_name, column_attribute);
        column_names.push_back(column_name);
        column_attributes.push_back(column_attribute);
    }

    // Add to schema: _tables and _columns
    ValueDict row;
    row["table_name"] = table_name;
    Handle t_handle = SQLExec::tables->insert(&row);  // Insert into _tables
    try {
        Handles c_handles;
        DbRelation &columns = SQLExec::tables->get_table(Columns::TABLE_NAME);
        try {
            for (uint i = 0; i < column_names.size(); i++) {
                row["column_name"] = column_names[i];
                row["data_type"] = Value(column_attributes[i].get_data_type() == ColumnAttribute::INT ? "INT" : "TEXT");
                c_handles.push_back(columns.insert(&row));  // Insert into _columns
            }

            // Finally, actually create the relation
            DbRelation &table = SQLExec::tables->get_table(table_name);
            if (statement->ifNotExists)
                table.create_if_not_exists();
            else
                table.create();

        } catch (...) {
            // attempt to remove from _columns
            try {
                for (auto const &handle: c_handles)
                    columns.del(handle);
            } catch (...) {}
            throw;
        }

    } catch (exception &e) {
        try {
            // attempt to remove from _tables
            SQLExec::tables->del(t_handle);
        } catch (...) {}
        throw;
    }
    return new QueryResult("created " + table_name);
}

QueryResult *SQLExec::create_index(const CreateStatement *statement) {
    Identifier index_name = statement->indexName;
    Identifier table_name = statement->tableName;

    // get underlying relation
    DbRelation &table = SQLExec::tables->get_table(table_name);

    // check that given columns exist in table
    const ColumnNames &table_columns = table.get_column_names();
    for (auto const &col_name: *statement->indexColumns)
        if (find(table_columns.begin(), table_columns.end(), col_name) == table_columns.end())
            throw SQLExecError(string("Column '") + col_name + "' does not exist in " + table_name);

    // insert a row for every column in index into _indices
    ValueDict row;
    row["table_name"] = Value(table_name);
    row["index_name"] = Value(index_name);
    row["index_type"] = Value(statement->indexType);
    row["is_unique"] = Value(string(statement->indexType) == "BTREE"); // assume HASH is non-unique --
    int seq = 0;
    Handles i_handles;
    try {
        for (auto const &col_name: *statement->indexColumns) {
            row["seq_in_index"] = Value(++seq);
            row["column_name"] = Value(col_name);
            i_handles.push_back(SQLExec::indices->insert(&row));
        }

        DbIndex &index = SQLExec::indices->get_index(table_name, index_name);
        index.create();

    } catch (...) {
        // attempt to remove from _indices
        try {  // if any exception happens in the reversal below, we still want to re-throw the original ex
            for (auto const &handle: i_handles)
                SQLExec::indices->del(handle);
        } catch (...) {}
        throw;  // re-throw the original exception (which should give the client some clue as to why it did
    }
    return new QueryResult("created index " + index_name);
}

// DROP ...
QueryResult *SQLExec::drop(const DropStatement *statement) {
    switch (statement->type) {
        case DropStatement::kTable:
            return drop_table(statement);
        case DropStatement::kIndex:
            return drop_index(statement);
        default:
            return new QueryResult("Only DROP TABLE and CREATE INDEX are implemented");
    }
}

QueryResult *SQLExec::drop_table(const DropStatement *statement) {
    Identifier table_name = statement->name;
    if (table_name == Tables::TABLE_NAME || table_name == Columns::TABLE_NAME)
        throw SQLExecError("cannot drop a schema table");

    ValueDict where;
    where["table_name"] = Value(table_name);

    // get the table
    DbRelation &table = SQLExec::tables->get_table(table_name);

    // remove any indices
    for (auto const &index_name: SQLExec::indices->get_index_names(table_name)) {
        DbIndex &index = SQLExec::indices->get_index(table_name, index_name);
        index.drop();  // drop the index
    }
    Handles *handles = SQLExec::indices->select(&where);
    for (auto const &handle: *handles)
        SQLExec::indices->del(handle);  // remove all rows from _indices for each index on this table
    delete handles;

    // remove from _columns schema
    DbRelation &columns = SQLExec::tables->get_table(Columns::TABLE_NAME);
    handles = columns.select(&where);
    for (auto const &handle: *handles)
        columns.del(handle);
    delete handles;

    // remove table
    table.drop();

    // finally, remove from _tables schema
    handles = SQLExec::tables->select(&where);
    SQLExec::tables->del(*handles->begin()); // expect only one row from select
    delete handles;

    return new QueryResult(string("dropped ") + table_name);
}

QueryResult *SQLExec::drop_index(const DropStatement *statement) {
    Identifier table_name = statement->name;
    Identifier index_name = statement->indexName;

    // drop index
    DbIndex &index = SQLExec::indices->get_index(table_name, index_name);
    index.drop();

    // remove rows from _indices for this index
    ValueDict where;
    where["table_name"] = Value(table_name);
    where["index_name"] = Value(index_name);
    Handles *handles = SQLExec::indices->select(&where);
    for (auto const &handle: *handles)
        SQLExec::indices->del(handle);
    delete handles;

    return new QueryResult("dropped index " + index_name);
}

// SHOW ...
QueryResult *SQLExec::show(const ShowStatement *statement) {
    switch (statement->type) {
        case ShowStatement::kTables:
            return show_tables();
        case ShowStatement::kColumns:
            return show_columns(statement);
        case ShowStatement::kIndex:
            return show_index(statement);
        default:
            throw SQLExecError("unrecognized SHOW type");
    }
}

QueryResult *SQLExec::show_index(const ShowStatement *statement) {
    ColumnNames *column_names = new ColumnNames;
    ColumnAttributes *column_attributes = new ColumnAttributes;
    column_names->push_back("table_name");
    column_attributes->push_back(ColumnAttribute(ColumnAttribute::TEXT));

    column_names->push_back("index_name");
    column_attributes->push_back(ColumnAttribute(ColumnAttribute::TEXT));

    column_names->push_back("column_name");
    column_attributes->push_back(ColumnAttribute(ColumnAttribute::TEXT));

    column_names->push_back("seq_in_index");
    column_attributes->push_back(ColumnAttribute(ColumnAttribute::INT));

    column_names->push_back("index_type");
    column_attributes->push_back(ColumnAttribute(ColumnAttribute::TEXT));

    column_names->push_back("is_unique");
    column_attributes->push_back(ColumnAttribute(ColumnAttribute::BOOLEAN));

    ValueDict where;
    where["table_name"] = Value(string(statement->tableName));
    Handles *handles = SQLExec::indices->select(&where);
    u_long n = handles->size();

    ValueDicts *rows = new ValueDicts;
    for (auto const &handle: *handles) {
        ValueDict *row = SQLExec::indices->project(handle, column_names);
        rows->push_back(row);
    }
    delete handles;
    return new QueryResult(column_names, column_attributes, rows, "successfully returned " + to_string(n) + " rows");
}

QueryResult *SQLExec::show_tables() {
    ColumnNames *column_names = new ColumnNames;
    column_names->push_back("table_name");

    ColumnAttributes *column_attributes = new ColumnAttributes;
    column_attributes->push_back(ColumnAttribute(ColumnAttribute::TEXT));

    Handles *handles = SQLExec::tables->select();
    u_long n = handles->size() - 3;

    ValueDicts *rows = new ValueDicts;
    for (auto const &handle: *handles) {
        ValueDict *row = SQLExec::tables->project(handle, column_names);
        Identifier table_name = row->at("table_name").s;
        if (table_name != Tables::TABLE_NAME && table_name != Columns::TABLE_NAME && table_name != Indices::TABLE_NAME)
            rows->push_back(row);
        else
            delete row;
    }
    delete handles;
    return new QueryResult(column_names, column_attributes, rows, "successfully returned " + to_string(n) + " rows");
}

QueryResult *SQLExec::show_columns(const ShowStatement *statement) {
    DbRelation &columns = SQLExec::tables->get_table(Columns::TABLE_NAME);

    ColumnNames *column_names = new ColumnNames;
    column_names->push_back("table_name");
    column_names->push_back("column_name");
    column_names->push_back("data_type");

    ColumnAttributes *column_attributes = new ColumnAttributes;
    column_attributes->push_back(ColumnAttribute(ColumnAttribute::TEXT));

    ValueDict where;
    where["table_name"] = Value(statement->tableName);
    Handles *handles = columns.select(&where);
    u_long n = handles->size();

    ValueDicts *rows = new ValueDicts;
    for (auto const &handle: *handles) {
        ValueDict *row = columns.project(handle, column_names);
        rows->push_back(row);
    }
    delete handles;
    return new QueryResult(column_names, column_attributes, rows, "successfully returned " + to_string(n) + " rows");
}

