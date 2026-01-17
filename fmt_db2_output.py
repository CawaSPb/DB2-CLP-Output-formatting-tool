#!/usr/bin/python3
"""
UNIX-style filter to reduce column widths in text tables.

Tables are recognized by:
- Header lines with column names (can be multi-line, bottom-aligned)
- Separator line consisting entirely of '-' characters for each column width

Multi-line header criteria:
- All header rows have spaces between columns (at gap positions)
- Column names are bottom-aligned within header rows
- Multi-line structure is preserved in output (not collapsed)

Columns are separated by single spaces. Justification is preserved.
Column width = max(max_header_line_length, max_value_length)
"""
import sys
import re


def find_columns(dash_line):
    """Find column positions from the dash separator line."""
    columns = []
    i = 0
    while i < len(dash_line):
        if dash_line[i] == '-':
            start = i
            while i < len(dash_line) and dash_line[i] == '-':
                i += 1
            columns.append((start, i))
        else:
            i += 1
    return columns


def find_gaps(columns):
    """Find gap positions between columns."""
    gaps = []
    for i in range(len(columns) - 1):
        gap_start = columns[i][1]
        gap_end = columns[i + 1][0]
        gaps.append((gap_start, gap_end))
    return gaps


def is_valid_header_line(line, columns, gaps):
    """Check if a line could be part of a multi-line header.
    
    A valid header line has spaces in all gap positions between columns.
    """
    for gap_start, gap_end in gaps:
        for pos in range(gap_start, min(gap_end, len(line))):
            if pos < len(line) and line[pos] != ' ':
                return False
    return True


def extract_value(line, start, end):
    """Extract a value from a line at given positions."""
    if start >= len(line):
        return ''
    return line[start:min(end, len(line))]


def detect_justification(raw_values):
    """Detect if column values are left or right justified."""
    for val in raw_values:
        stripped = val.strip()
        if not stripped:
            continue
        left_spaces = len(val) - len(val.lstrip())
        right_spaces = len(val) - len(val.rstrip())
        
        if left_spaces > 0 and right_spaces == 0:
            return 'right'
        elif right_spaces > 0 and left_spaces == 0:
            return 'left'
        elif left_spaces > right_spaces:
            return 'right'
    return 'left'


def collect_header_lines(lines, dash_line_idx, columns):
    """Collect all header lines above the dash line.
    
    Returns list of header line indices in top-to-bottom order.
    """
    gaps = find_gaps(columns)
    header_indices = []
    
    # Go backwards from dash line to find header lines
    idx = dash_line_idx - 1
    while idx >= 0:
        line = lines[idx]
        # Empty line stops header collection
        if not line.strip():
            break
        # Check if this line has valid header structure (spaces in gaps)
        if is_valid_header_line(line, columns, gaps):
            header_indices.append(idx)
            idx -= 1
        else:
            break
    
    # Reverse to get top-to-bottom order
    header_indices.reverse()
    return header_indices


def extract_multiline_headers(lines, header_indices, columns):
    """Extract header text for each column from each header line.
    
    Returns a list of lists: header_texts[col_idx][line_idx] = text
    Column names are bottom-aligned within header rows.
    """
    num_cols = len(columns)
    num_lines = len(header_indices)
    
    # Extract raw text for each column from each header line
    header_texts = []
    for col_idx, (start, end) in enumerate(columns):
        col_texts = []
        for idx in header_indices:
            line = lines[idx]
            text = extract_value(line, start, end).strip()
            col_texts.append(text)
        header_texts.append(col_texts)
    
    return header_texts


def process_table(lines, header_indices, dash_line_idx, data_lines, columns):
    """Process a detected table and return reformatted lines."""
    
    num_header_lines = len(header_indices)
    
    # Extract header texts preserving line structure
    header_texts = extract_multiline_headers(lines, header_indices, columns)
    
    # Extract all data values
    raw_values = []
    stripped_values = []
    for line in data_lines:
        raw_row = [extract_value(line, start, end) for start, end in columns]
        stripped_row = [v.strip() for v in raw_row]
        raw_values.append(raw_row)
        stripped_values.append(stripped_row)
    
    # Determine justification for each column
    justifications = []
    for col_idx in range(len(columns)):
        col_raw = [row[col_idx] for row in raw_values]
        justifications.append(detect_justification(col_raw))
    
    # Calculate new column widths: max(max_header_line_len, max_value_len)
    new_widths = []
    for col_idx in range(len(columns)):
        # Max length of any single header line for this column
        max_header_line_len = max((len(t) for t in header_texts[col_idx]), default=0)
        max_val_len = max((len(row[col_idx]) for row in stripped_values), default=0)
        new_widths.append(max(max_header_line_len, max_val_len))
    
    # Format output
    result = []
    
    # Multi-line header - preserve structure, bottom-aligned
    for line_idx in range(num_header_lines):
        header_parts = []
        for col_idx in range(len(columns)):
            text = header_texts[col_idx][line_idx]
            # Left justify header text within column width
            header_parts.append(text.ljust(new_widths[col_idx]))
        result.append(' '.join(header_parts).rstrip())
    
    # Dash line
    dash_parts = ['-' * new_widths[i] for i in range(len(columns))]
    result.append(' '.join(dash_parts))
    
    # Data lines with preserved justification
    for row in stripped_values:
        parts = []
        for i, val in enumerate(row):
            if justifications[i] == 'right':
                parts.append(val.rjust(new_widths[i]))
            else:
                parts.append(val.ljust(new_widths[i]))
        result.append(' '.join(parts).rstrip())
    
    return result


def is_dash_line(line):
    """Check if a line is a valid dash separator line."""
    if not line or not line.strip():
        return False
    return bool(re.match(r'^[\- ]+$', line) and '-' in line)


def main():
    content = sys.stdin.read()
    # Remove UTF-8 BOM if present
    if content.startswith('\ufeff'):
        content = content[1:]
    # Handle Windows line endings
    content = content.replace('\r\n', '\n').replace('\r', '\n')
    lines = content.split('\n')
    # Remove trailing empty line if present
    if lines and lines[-1] == '':
        lines = lines[:-1]
    
    # Track which lines have been consumed by tables
    consumed = set()
    output_map = {}  # Maps line index to output lines
    
    i = 0
    while i < len(lines):
        # Look for dash separator line
        if is_dash_line(lines[i]):
            dash_line_idx = i
            dash_line = lines[i]
            columns = find_columns(dash_line)
            
            # Collect header lines above dash line
            header_indices = collect_header_lines(lines, dash_line_idx, columns)
            
            if not header_indices:
                # No valid header found, pass through
                print(lines[i])
                i += 1
                continue
            
            # Collect data lines until empty line or end
            data_lines = []
            j = dash_line_idx + 1
            while j < len(lines):
                line = lines[j]
                if not line.strip():
                    break
                # Stop if next line would be a new dash separator
                if is_dash_line(line):
                    break
                data_lines.append(line)
                j += 1
            
            # Process and output table
            if data_lines:
                formatted = process_table(lines, header_indices, dash_line_idx, data_lines, columns)
                
                # Mark all header lines and dash line as consumed
                for idx in header_indices:
                    consumed.add(idx)
                consumed.add(dash_line_idx)
                for k in range(dash_line_idx + 1, j):
                    consumed.add(k)
                
                # Output formatted table at first header line position
                first_header_idx = header_indices[0]
                output_map[first_header_idx] = formatted
            else:
                # No data lines, just output reduced header preserving structure
                header_texts = extract_multiline_headers(lines, header_indices, columns)
                widths = [max((len(t) for t in col_texts), default=0) for col_texts in header_texts]
                formatted = []
                for line_idx in range(len(header_indices)):
                    parts = [header_texts[col_idx][line_idx].ljust(widths[col_idx]) 
                             for col_idx in range(len(columns))]
                    formatted.append(' '.join(parts).rstrip())
                formatted.append(' '.join('-' * w for w in widths))
                
                for idx in header_indices:
                    consumed.add(idx)
                consumed.add(dash_line_idx)
                
                first_header_idx = header_indices[0]
                output_map[first_header_idx] = formatted
            
            i = j
            continue
        
        i += 1
    
    # Output all lines in order
    for i in range(len(lines)):
        if i in output_map:
            for line in output_map[i]:
                print(line)
        elif i not in consumed:
            print(lines[i])


if __name__ == '__main__':
    main()
