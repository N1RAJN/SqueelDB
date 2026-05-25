#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define COLUMN_USERNAME_SIZE 32
#define COLUMN_EMAIL_SIZE 255
#define size_of_attribute(Struct, Attrib) (sizeof((Struct *)0)->Attrib)
#define PAGE_SIZE 4096
#define MAX_NO_OF_PAGES 100

/* Row structure to store records.
 * Table structure to store the rows.
 * Divide the table into multiple pages (4kb)
 *
 * Need to store the data such that the values are stored contiguously, so as to
 * efficiently read and write.
 *
 * For now, hard coded database i.e, only one table
 * with 3 columns (id, name, email).
 */
typedef struct {
    char *buffer;
    size_t buffer_length;
    ssize_t input_length;
} InputBuffer;

typedef enum {
    STATEMENT_INSERT,
    STATEMENT_SELECT,
    STATEMENT_DELETE
} StatementType;

typedef enum { META_SUCCESS, META_ERROR } MetaCommandResult;

typedef enum {
    PREPARE_SUCCESS,
    PREPARE_SYNTAX_ERROR,
    PREPARE_ERROR
} PrepareResult;

typedef enum {
    EXECUTE_SUCCESS,
    EXECUTE_TABLE_FULL,
    EXECUTE_ERROR
} ExecuteResult;

typedef struct {
    uint32_t id;
    char username[COLUMN_USERNAME_SIZE];
    char email[COLUMN_EMAIL_SIZE];
} Row;

typedef struct {
    StatementType type;
    Row row_to_insert;
} Statement;

const uint32_t ID_SIZE = size_of_attribute(Row, id);
const uint32_t USERNAME_SIZE = size_of_attribute(Row, username);
const uint32_t EMAIL_SIZE = size_of_attribute(Row, email);
const uint32_t ID_OFFSET = 0;
const uint32_t USERNAME_OFFSET = ID_SIZE;
const uint32_t EMAIL_OFFSET = ID_SIZE + USERNAME_SIZE;
const uint32_t ROW_SIZE = ID_SIZE + USERNAME_SIZE + EMAIL_SIZE;
const uint32_t ROWS_PER_PAGE = PAGE_SIZE / ROW_SIZE;
const uint32_t MAX_ROWS_TABLE = ROWS_PER_PAGE * MAX_NO_OF_PAGES;

typedef struct {
    uint32_t num_of_rows;
    void *pages[MAX_NO_OF_PAGES];
} Table;

Table *new_table() {
    Table *table = (Table *)malloc(sizeof(Table));
    table->num_of_rows = 0;
    for (uint32_t i = 0; i < MAX_NO_OF_PAGES; ++i)
        table->pages[i] = NULL;
    return table;
}

void free_table(Table *table) {
    for (uint32_t i = 0; i < MAX_NO_OF_PAGES; ++i)
        free(table->pages[i]);
    free(table);
}

void *row_slot(Table *table, uint32_t row_num) {
    uint32_t page_num = row_num / ROWS_PER_PAGE;
    void *page = table->pages[page_num];
    if (page == NULL) {
        page = table->pages[page_num] = malloc(PAGE_SIZE);
    }
    uint32_t row_offset = row_num % ROWS_PER_PAGE;
    uint32_t byte_offset = row_offset * ROW_SIZE;
    return page + byte_offset;
};

void print_row(Row *row) {
    printf("(%d, %s, %s)\n", row->id, row->username, row->email);
}

void serialize_row(Row *source, void *destination) {
    memcpy(destination + ID_OFFSET, &(source->id), ID_SIZE);
    memcpy(destination + USERNAME_OFFSET, &(source->username), USERNAME_SIZE);
    memcpy(destination + EMAIL_OFFSET, &(source->email), EMAIL_SIZE);
};

void deserialize_row(void *source, Row *destination) {
    memcpy(&(destination->id), source + ID_OFFSET, ID_SIZE);
    memcpy(&(destination->username), source + USERNAME_OFFSET, USERNAME_SIZE);
    memcpy(&(destination->email), source + EMAIL_OFFSET, EMAIL_SIZE);
};

InputBuffer *new_input_buffer() {
    InputBuffer *input_buffer = (InputBuffer *)malloc(sizeof(InputBuffer));
    input_buffer->buffer = NULL;
    input_buffer->buffer_length = 0;
    input_buffer->input_length = 0;
    return input_buffer;
}

void print_prompt() { printf("SqueelDB :> "); }

void read_input(InputBuffer *input_buffer) {
    ssize_t bytes_read =
        getline(&(input_buffer->buffer), &(input_buffer->buffer_length), stdin);
    if (bytes_read <= 0) {
        printf("Error reading promopt.\n");
        exit(EXIT_FAILURE);
    }
    input_buffer->buffer[bytes_read - 1] = 0;
    input_buffer->input_length = bytes_read - 1;
}

void close_buffer(InputBuffer *input_buffer) {
    free(input_buffer->buffer);
    free(input_buffer);
}

PrepareResult prepare_statement(InputBuffer *input_buffer, Statement *stmnt) {
    if (strncmp(input_buffer->buffer, "insert", 6) == 0) {
        stmnt->type = STATEMENT_INSERT;
        int args_read = sscanf(
            input_buffer->buffer, "insert %d %s %s", &(stmnt->row_to_insert.id),
            stmnt->row_to_insert.username, stmnt->row_to_insert.email);
        if (args_read < 3)
            return PREPARE_SYNTAX_ERROR;
        return PREPARE_SUCCESS;
    } else if (strncmp(input_buffer->buffer, "select", 6) == 0) {
        stmnt->type = STATEMENT_SELECT;
        return PREPARE_SUCCESS;
    } else if (strncmp(input_buffer->buffer, "delete", 6) == 0) {
        stmnt->type = STATEMENT_DELETE;
        return PREPARE_SUCCESS;
    } else {
        return PREPARE_ERROR;
    }
}

MetaCommandResult execute_meta_command(InputBuffer *input_buffer) {
    if (strcmp(input_buffer->buffer, ".exit") == 0) {
        printf("Exiting... \n");
        close_buffer(input_buffer);
        exit(EXIT_SUCCESS);
    } else {
        return META_ERROR;
    }
    // Once more meta commands
    return META_SUCCESS;
}

ExecuteResult execute_insert(Statement *stmnt, Table *table) {
    if (table->num_of_rows >= MAX_ROWS_TABLE) {
        return EXECUTE_TABLE_FULL;
    }
    void *destination_row = row_slot(table, table->num_of_rows);
    Row *source_row = &(stmnt->row_to_insert);

    serialize_row(source_row, destination_row);
    table->num_of_rows += 1;

    return EXECUTE_SUCCESS;
}

ExecuteResult execute_select(Statement *stmnt, Table *table) {
    Row row;
    for (uint32_t row_num = 0; row_num < table->num_of_rows; ++row_num) {
        deserialize_row(row_slot(table, row_num), &row);
        print_row(&row);
    }
    return EXECUTE_SUCCESS;
}

ExecuteResult execute_statement(Statement *stmnt, Table *table) {
    switch (stmnt->type) {
    case STATEMENT_SELECT:
        return execute_select(stmnt, table);
    case STATEMENT_INSERT:
        return execute_insert(stmnt, table);
    case STATEMENT_DELETE:
        printf("Execute delete command.\n");
        return EXECUTE_SUCCESS;
    default:
        printf("Unknown command.\n");
        return EXECUTE_ERROR;
    }
}

int main(void) {
    InputBuffer *input_buffer = new_input_buffer();
    Table *table = new_table();
    while (true) {
        print_prompt();
        read_input(input_buffer);

        if (strncmp(input_buffer->buffer, ".", 1) == 0) {
            MetaCommandResult res = execute_meta_command(input_buffer);
            switch (res) {
            case META_SUCCESS:
                continue;
            case META_ERROR:
                printf("Unrecognized command %s\n", input_buffer->buffer);
                continue;
            }
        }

        Statement stmt;
        switch (prepare_statement(input_buffer, &stmt)) {
        case PREPARE_SUCCESS:
            break;
        case PREPARE_SYNTAX_ERROR:
            printf("Syntax error. Couldn't parse the command.\n");
            continue;
        case PREPARE_ERROR:
            printf("Couldn't Prepare the query.\n");
            break;
        }

        switch (execute_statement(&stmt, table)) {
        case EXECUTE_TABLE_FULL:
            printf("Table full. Couldn't insert.\n");
            break;
        case EXECUTE_SUCCESS:
            printf("Executed.\n");
            break;
        case EXECUTE_ERROR:
            printf("Error executing the command.\n");
            break;
        }
    }
    return 0;
}
