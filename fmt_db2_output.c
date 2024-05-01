/*
  DB2 CLP output formatting 

  (C) Alexander Veremyev 2016
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#define INITIAL_LINE_BUFFER_SIZE 2048
#define INITIAL_LINES_CONTAINER_SIZE 4096
#define INITIAL_COLUMNS_CONTAINER_SIZE 1024

#define INPUT stdin


struct columnDescription {
  char             name[129];
  unsigned short   nameLength;
  char             printFormat[64];
  size_t           offset;
  size_t           length;
  long             leftPad;
  long             rightPad;
};


/*************************************************************
 * Get line from input
 *
 *************************************************************/
char* getLine (FILE * f) {
  char * buff, * _buff;
  char * str_ptr;
  size_t str_length, processed_length;
  size_t buff_size;


  buff_size        = INITIAL_LINE_BUFFER_SIZE;
  processed_length = 0;
  buff             = NULL;


  while (1) {
    if ( (_buff = realloc(buff, buff_size)) == NULL  ||  errno == ENOMEM ) {
      fprintf(stderr, "Not enough memory or memory allocation error\nPartial input processing\n");
      free(buff);

      return NULL;
    }
    buff = _buff;

    //  set pointer to the new string  section
    str_ptr = buff + processed_length;

    // Get string and check if there was any error
    if (fgets (str_ptr, buff_size - processed_length, f) == NULL) {
      if (!feof(f)) {
        // Input error
        fprintf(stderr, "Input read error\nPartial input processing\n");
        free(buff);

        return NULL;
      } else if (processed_length == 0) {
        // EOF at the beginning of new line
        free(buff);

        return NULL;
      } else {
        // EOF at the start of new buffer section
        return buff;
      }
    } else {
      str_length = strlen(str_ptr);

      if (str_ptr[str_length - 1] == '\n') {
        // End Of Line, string reading is done.
        str_ptr[str_length - 1] = 0;

        return buff;
      } else {
        processed_length += str_length;
        buff_size *= 2;
      }
    }
  }
}


/*************************************************************
 * Load and flash input which is non-relevant to the resultset
 * printing.
 *************************************************************/
void flushIrrelevantLines() {
  char * line;

  while ((line = getLine(INPUT)) != NULL) {
    printf("%s\n", line);

    if (line[0] == 0) {
      free(line);
      return;
    }

    free(line);
  }
}


/*************************************************************
 * Load input
 *
 *************************************************************/
char **getInput(int sample_size) {
  char **inputLines, **_inputLines;
  unsigned long input_buffer_size;
  unsigned long lines, count;

  inputLines        = NULL;
  input_buffer_size = INITIAL_LINES_CONTAINER_SIZE;
  lines             = 0;

  while (1) {
    // Allocate/reallocate memory
    if ( (_inputLines = realloc(inputLines, input_buffer_size*(sizeof(char *)))) == NULL  ||  errno == ENOMEM ) {
      for (count = 0; count < lines; count++)
        free(inputLines[ count ]);

      free (inputLines);
      fprintf(stderr, "Not enough memory or memory allocation error\n");

      return NULL;
    }
    inputLines = _inputLines;

    while ( lines < input_buffer_size - 1  &&  (( lines < sample_size) || (sample_size == -1)) ) {
      if ((inputLines[lines] = getLine(INPUT)) == NULL) {
        // End of input
        return inputLines;
      }

      lines++;
    }

    if (lines == sample_size) {
      inputLines[lines] = NULL; // End of input marker
      return inputLines;
    }

    input_buffer_size *= 2;
  }
}


/*************************************************************
 * Flush specified part of already preloaded lines and
 * the rest of input
 *
 *************************************************************/
void flushLines(char **lines) {
  char * line;

  for (unsigned long count = 0; lines[count] != NULL; count++) {
    printf("%s\n", lines[count]);
    free(lines[count]);
  }
  free(lines);

  while ((line = getLine(INPUT)) != NULL) {
    printf("%s\n", line);
    free(line);
  }
}


/*************************************************************
 * Get header info
 *
 *************************************************************/
struct columnDescription *parse_header(char *lineNames, char *lineDelimiters) {
  int columns_buffer_size = INITIAL_COLUMNS_CONTAINER_SIZE;
  struct columnDescription *columnsContainer, *_tempColumnsContainer;
  long columns, count;

  // Allocate memory
  if ( (columnsContainer = malloc(columns_buffer_size*sizeof(struct columnDescription))) == NULL ) {
    fprintf(stderr, "Not enough memory or memory allocation error whileprocessing resultset header\nInitial allocation\n");
    return NULL;
  }

  /***************************************************************************
   * Parse delemeters line: '------------ -------- ------------- ---- ....'
   *
   ***************************************************************************/
  columns = 0;
  columnsContainer[columns].offset    =  0;
  columnsContainer[columns].leftPad   = -1;
  columnsContainer[columns].rightPad  = -1;

  for (count = 0; lineDelimiters[count] != 0; count++) {
    switch (lineDelimiters[count]) {
      case '-':
        // Next char of the current column. Do nothing
        break;

      case ' ':
        // Column break. Collect column info and skip whitespace.
        columnsContainer[columns].length   = count - columnsContainer[columns].offset;

        columns++;

        // Check if it's necessary to reserve additional space for columns description container
        if (columns + 1 >= columns_buffer_size) {
          columns_buffer_size *= 2;

          if ( (_tempColumnsContainer = realloc( columnsContainer, columns_buffer_size*sizeof(struct columnDescription) )) == NULL ) {
            fprintf(stderr, "Not enough memory or memory allocation error while processing resultset header\nReallocation\n");
            free(columnsContainer);
            return NULL;
          }
          columnsContainer = _tempColumnsContainer;
        }

        columnsContainer[columns].offset   = count + 1;
        columnsContainer[columns].leftPad  = -1;
        columnsContainer[columns].rightPad = -1;
        break;

      default:
        // Non-expected symbol. Stop processing
        free(columnsContainer);
        return NULL;
    }
  }

  columnsContainer[columns].length     = count - columnsContainer[columns].offset;
  columnsContainer[columns + 1].length = -1; // End of columns marker


  /***************************************************************************
   * Collect column names
   *
   ***************************************************************************/
  for (count = 0; columnsContainer[count].length != -1; count++) {
    // Check header format (no zero-length columns)
    if (columnsContainer[count].length == 0) {
      free(columnsContainer);

      return NULL;
    }

    // 128 is a limit of column name length in DB2 UDB
    // Nevertheless, let's check it since space for name is limited by 129 characters
    columnsContainer[count].nameLength = (columnsContainer[count].length < 128)? columnsContainer[count].length : 128;

    strncpy(
      columnsContainer[count].name
     ,lineNames + columnsContainer[count].offset
     ,columnsContainer[count].nameLength
    );
    columnsContainer[count].name[ columnsContainer[count].nameLength ] = 0;

    // Remove trailing spaces from column name
    for (short charCounter = columnsContainer[count].nameLength - 1; charCounter >= 0; charCounter--) {
      if (columnsContainer[count].name[charCounter] == ' ') {
        columnsContainer[count].name[charCounter] = 0;
        columnsContainer[count].nameLength--;
      } else {
        break;
      }
    }

    // Check that column has correct name
    if (columnsContainer[count].nameLength == 0) {
      free(columnsContainer);
      return NULL;
    }
  }

  return columnsContainer;
}


/*************************************************************
 * Check, that line looks like valid resultset row
 * Returns:
 *  0 - valid row
 *  1 - SQL error or warning
 * -1 - non-DB2 output
 *
 *************************************************************/
int is_valid_row(struct columnDescription *columns, char* line, int state) {
  size_t length;
  long   count, offset;

  if (state == -1)  return -1;

  length = strlen(line);

  for (count = 0; columns[count].length != -1; count++) {
    offset = columns[count].offset + columns[count].length;

    if ( offset > length  ||  (line[offset] != ' ' && line[offset] != 0) ) {
      if (state == 1  ||  strncmp(line, "SQL", 3) == 0) {
        // SQL error or warning
        return 1;
      } else {
        return -1;
      }
    }
  }

  return 0;
}


/*************************************************************
 * Print rowset line.
 *
 *************************************************************/
void print_row(struct columnDescription *columns, char *line) {
  long count;

  for (count = 0; columns[count].length != -1; count++) {
    printf(columns[count].printFormat, line + columns[count].offset);
  }

  free(line);
}


/*************************************************************
 * Analyze rowset (pass 1)
 *
 *************************************************************/
int analyze_rowset(struct columnDescription *columns, char **lines) {
  long count, columnCount;
  long leftPad, rightPad;
  size_t offset, length;


  // Iterate through lines to get left/right padding info
  for (count = 2; lines[count] != NULL; count++) {
    // Check if it's the end of result set. If yes, break
    if (lines[count][0] == 0)
      break;

    switch (is_valid_row(columns, lines[count], 0)) {
      case 1:
        // SQL error or warning. Skip non-relevant lines up to empty line.
        while (lines[count] != NULL  &&  lines[count][0] != 0) {
          count++;
        }

        // Go to next input line
        continue;

      case -1:
        return -1;
    }

    for (columnCount = 0; columns[columnCount].length != -1; columnCount++) {
      // Get left pad
      offset  = columns[columnCount].offset;
      length  = columns[columnCount].length;
      leftPad = 0;
      while (lines[count][offset + leftPad] == ' ' && leftPad < length)
         leftPad++;

      if (leftPad == columns[columnCount].length)
        // Empty value. Don't take it into account.
        continue;

      if (columns[columnCount].leftPad == -1 || columns[columnCount].leftPad > leftPad)
        columns[columnCount].leftPad = leftPad;


      // Get right pad
      rightPad = 0;
      while (lines[count][offset + length - rightPad - 1] == ' '  &&  rightPad < length)
        rightPad++;

      if (columns[columnCount].rightPad == -1  ||  columns[columnCount].rightPad > rightPad)
        columns[columnCount].rightPad = rightPad;
    }
  }

  return 0;
}


/**********************************************************************************
 * Process header
 *
 **********************************************************************************/
void process_header(struct columnDescription *columns) {
  struct columnDescription *column;
  char headerPrintFormat[64];
  long count, columnCount;

  // Column names
  for (count = 0; columns[count].length != -1; count++) {
    column    = &(columns[count]);

    if (column->leftPad == -1) {
      // Empty column
      column->length = column->nameLength;

      snprintf(headerPrintFormat,   64, (columns[count+1].length == -1) ? "%%-%d.%ds\n" : "%%-%d.%ds ", column->nameLength, column->nameLength);
      snprintf(column->printFormat, 64, (columns[count+1].length == -1) ? "%%-%d.%ds\n" : "%%-%d.%ds ", column->nameLength, column->nameLength);

    } else if (column->nameLength <= column->length - (column->leftPad + column->rightPad) ) {
      // Value is longer or equal than column name
      column->offset += column->leftPad;
      column->length -= (column->leftPad + column->rightPad);

      snprintf(headerPrintFormat,   64, (columns[count+1].length == -1) ? "%%-%d.%ds\n" : "%%-%d.%ds ", column->length, column->nameLength);
      snprintf(column->printFormat, 64, (columns[count+1].length == -1) ? "%%-%d.%ds\n" : "%%-%d.%ds ", column->length, column->length);

    } else {
      // Name is longer than column values
      column->offset += column->leftPad;
      column->length -= (column->leftPad + column->rightPad);

      if (column->leftPad <= column->rightPad) {
        // Left padded column
        snprintf(column->printFormat, 64, (columns[count+1].length == -1) ? "%%-%d.%ds\n" : "%%-%d.%ds ", column->nameLength, column->length);
      } else {
        // Special case, values are right justified
        snprintf(column->printFormat, 64, (columns[count+1].length == -1) ? "%%%d.%ds\n"  : "%%%d.%ds ",  column->nameLength, column->length);
      }

      sprintf(headerPrintFormat, (columns[count+1].length == -1) ? "%%-%d.%ds\n" : "%%-%d.%ds ", column->nameLength, column->nameLength);
    }

    printf(headerPrintFormat, column->name);
  }

  for (columnCount = 0; columns[columnCount].length != -1; columnCount++) {
    size_t length;

    length = (columns[columnCount].nameLength > columns[columnCount].length)?
                   columns[columnCount].nameLength : columns[columnCount].length;
    for (count = 0; count < length; count++) {
      putchar('-');
    }

    if (columns[columnCount+1].length != -1)
      printf(" ");
  }

  printf("\n");
}

/*************************************************************
 * Flush row
 * Returns current state of processing:
 *  1  - next line is a part of SQL warning/error message 
 *       in the middle of resultset
 *  0  - next line is a common rowset row
 * -1  - rowset processing is completed
 *************************************************************/
int process_row(struct columnDescription *columns, char *line, int state) {
  switch (state) {
    case 1:
      // SQL error or warning processing
      if (line[0] == 0) {
        // Empty line (end of SQL warning/error marker)
        state = 0;
      }

      printf("%s\n", line);
      free(line);

      return state;

    case 0:
      if ( (state = is_valid_row(columns, line, state)) == 0 ) {
        print_row(columns, line);
      } else {
        printf("%s\n", line);
        free(line);
      }

      return state;

    case -1:
      // Non-resultset lines processing
      printf("%s\n", line);
      free(line);

      return -1;
  }
}

/*************************************************************
 * Flush preloaded rowset
 * Returns current state of output processing:
 *  1  - next line is a part of SQL warning/error message 
 *       in the middle of resultset
 *  0  - next line is a common rowset row
 * -1  - rowset processing is completed
 *************************************************************/
int process_rowset_preloaded(struct columnDescription *columns, char **lines) {
  long count;
  int processing_state = 0;

  // Iterate through lines
  for (count = 2; lines[count] != NULL; count++) {
    processing_state = process_row(columns, lines[count], processing_state);
  }

  return processing_state;
}


/*************************************************************
 * Flush rowset
 * Returns:
 *  1  - next line is a part of SQL warning/error message 
 *       in the middle of resultset
 *  0  - next line is a common rowset row
 * -1  - rowset processing is completed
 *************************************************************/
int process_rowset(struct columnDescription *columns, int processing_state) {
  char *line;

  while ( (line = getLine(INPUT)) != NULL ) {
    processing_state = process_row(columns, line, processing_state);
  }

  return processing_state;
}


int process_input(int sample_size) {
  char  **inputLines;
  size_t  headerLength;
  struct columnDescription  *columnsContainer;


  flushIrrelevantLines();

  if ((inputLines = getInput(sample_size)) == NULL) {
    return 4;
  }

  // Check if correct DB2 output header is presented (min 3 lines
  if (inputLines[0] == NULL  ||  inputLines[1] == NULL  ||  inputLines[2] == NULL) {
    flushLines(inputLines);
    return 5;
  }

  headerLength = strlen(inputLines[0]);
  if (strlen(inputLines[1]) != headerLength) {
    // It's not a DB2 output
    flushLines(inputLines);
    return 6;
  }


  // Parse column headers
  if ((columnsContainer = parse_header(inputLines[0], inputLines[1])) == NULL) {
    // It's not correct DB2 header
    flushLines(inputLines);
    return 7;
  }

  // Analaze rowset (pass 1)
  if (analyze_rowset(columnsContainer, inputLines) != 0) {
    free(columnsContainer);
    flushLines(inputLines);
    return 8;
  }

  // Print header
  process_header(columnsContainer);
  free(inputLines[0]);
  free(inputLines[1]);


  int processing_state;

  // Print preloaded rowset
  processing_state = process_rowset_preloaded(columnsContainer, inputLines);
  free(inputLines);

  // Process the rest of input
  processing_state = process_rowset(columnsContainer, processing_state);

  free(columnsContainer);

  return 0;
}

void print_usage(void) {
  printf("Usage: format_db2_output [sample_size]\n");
  printf("  format_db2_output takes data from the standard input and prints it to standard output\n");
  printf("  <sample_size> is a number of rows taken to produce output format\n");
  printf("  if <sample_size> is ommitted, then whole row set is used to prepare format\n");
}

int main(int argc, char *argv[]) {
  int sample_size = -1;

  if (argc == 1) {
    sample_size = -1;
  } else if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-help") == 0 || strcmp(argv[1], "-h") == 0)) {
    print_usage();

    return 1;
  } else if (argc == 2 && sscanf(argv[1], "%d", &sample_size) != 1) {
    fprintf(stderr, "Wrong argument '%s'.\n\n", argv[1]);

    print_usage();

    return 2;
  } else if (argc > 2){
    fprintf(stderr, "Wrong number of arguments.\n\n", argv[1]);
    print_usage();

    return 3;
  }

  return process_input(sample_size);
}


