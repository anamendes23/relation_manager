/**
 * @file SQLExec.h - SQLExec class
 * @author Kevin Lundeen
 * @author Ana Mendes
 * @author Erika Skornia-Olsen
 * @see "Seattle University, CPSC5300, Spring 2022"
 */
#pragma once

#include <exception>
#include <string>
#include "SQLParser.h"
#include "schema_tables.h"

/**
 * @class SQLExecError - exception for SQLExec methods
 */
class SQLExecError : public std::runtime_error {
public:
    explicit SQLExecError(std::string s) : runtime_error(s) {}
};


/**
 * @class QueryResult - data structure to hold all the returned data for a query execution
 */
class QueryResult {
public:
    QueryResult() : column_names(nullptr), column_attributes(nullptr), rows(nullptr), message("") {}

    QueryResult(std::string message) : column_names(nullptr), column_attributes(nullptr), rows(nullptr),
                                       message(message) {}

    QueryResult(ColumnNames *column_names, ColumnAttributes *column_attributes, ValueDicts *rows, std::string message)
            : column_names(column_names), column_attributes(column_attributes), rows(rows), message(message) {}

    virtual ~QueryResult();

    ColumnNames *get_column_names() const { return column_names; }

    ColumnAttributes *get_column_attributes() const { return column_attributes; }

    ValueDicts *get_rows() const { return rows; }

    const std::string &get_message() const { return message; }

    friend std::ostream &operator<<(std::ostream &stream, const QueryResult &qres);

protected:
    ColumnNames *column_names;
    ColumnAttributes *column_attributes;
    ValueDicts *rows;
    std::string message;
};


/**
 * @class SQLExec - execution engine
 */
class SQLExec {
public:
    /**
     * Execute the given SQL statement.
     * @param statement   the Hyrise AST of the SQL statement to execute
     * @returns           the query result (freed by caller)
     */
    static QueryResult *execute(const hsql::SQLStatement *statement);

protected:
    // the one place in the system that holds the _tables table and _indices table
    static Tables *tables;
    static Indices *indices;

    // recursive decent into the AST
    /**
     * @brief calls appropriate create function to either create a table or an index
     * 
     * @param statement to create table or index
     * @return QueryResult* result summary of appropriate create funtion
     */
    static QueryResult *create(const hsql::CreateStatement *statement);

    /**
     * @brief creates a table and add it to the relational manager
     * 
     * @param statement with parts of SQL query
     * @return QueryResult* result summary of creating a table
     */
    static QueryResult *create_table(const hsql::CreateStatement *statement);

    /**
     * @brief creates an index for a specific table
     * 
     * @param statement with parts of SQL query
     * @return QueryResult* result summary of creating an index
     */
    static QueryResult *create_index(const hsql::CreateStatement *statement);

    /**
     * @brief calls appropriate drop function to either drop a table or an index
     * 
     * @param statement to drop table or index
     * @return QueryResult* result summary of appropriate drop funtion
     */
    static QueryResult *drop(const hsql::DropStatement *statement);

    /**
     * @brief drops a table and removes all indices associated with it
     * 
     * @param statement with parts of SQL query
     * @return QueryResult* result summary of dropping a table
     */
    static QueryResult *drop_table(const hsql::DropStatement *statement);

    /**
     * @brief drops specified index
     * 
     * @param statement with parts of SQL query
     * @return QueryResult* result of dropping the index
     */
    static QueryResult *drop_index(const hsql::DropStatement *statement);

    /**
     * @brief calls appropriate show function to either show tables, columns or indices
     * 
     * @param statement to show tables, columns or indices
     * @return QueryResult* result summary of appropriate show funtion
     */
    static QueryResult *show(const hsql::ShowStatement *statement);

    /**
     * @brief displays a list of tables in the relational manager
     * 
     * @return QueryResult* list of tables
     */
    static QueryResult *show_tables();

    /**
     * @brief displays all columns of the specified table
     * 
     * @param statement with parts of SQL query
     * @return QueryResult* list of columns of a table
     */
    static QueryResult *show_columns(const hsql::ShowStatement *statement);

    /**
     * @brief shows indices of a table
     * 
     * @param statement with parts of SQL query
     * @return QueryResult* list of indices of a table
     */
    static QueryResult *show_index(const hsql::ShowStatement *statement);

    /**
     * @brief inserts a row into a table
     * 
     * @param statement with parts of SQL query
     * @return QueryResult* result summary of adding a row to a table
     */
    static QueryResult *insert(const hsql::InsertStatement *statement);

    /**
     * @brief deletes a row from a table
     * 
     * @param statement with parts of SQL query
     * @return QueryResult* result summary of deleting a row from table
     */
    static QueryResult *del(const hsql::DeleteStatement *statement);

    /**
     * @brief selects rows from a table
     * 
     * @param statement with parts of SQL query
     * @return QueryResult* list of rows from table
     */
    static QueryResult *select(const hsql::SelectStatement *statement);

    /**
     * Pull out column name and attributes from AST's column definition clause
     * @param col                AST column definition
     * @param column_name        returned by reference
     * @param column_attributes  returned by reference
     */
    static void
    column_definition(const hsql::ColumnDefinition *col, Identifier &column_name, ColumnAttribute &column_attribute);
};

