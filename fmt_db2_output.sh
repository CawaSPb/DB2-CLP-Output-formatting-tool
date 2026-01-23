#!/bin/bash
# UNIX-style filter to reduce column widths in text tables.
# Uses awk for processing with bash wrapper for BOM/line-ending handling.
#
# Tables are recognized by:
# - Header lines with column names (can be multi-line, bottom-aligned)
# - Separator line consisting entirely of '-' characters for each column width
#
# Multi-line header criteria:
# - All header rows have spaces between columns (at gap positions)
# - Column names are bottom-aligned within header rows
# - Multi-line structure is preserved in output
#
# Usage: cat input.txt | ./fmt_db2_output.sh
#        ./fmt_db2_output.sh < input.txt

# Remove BOM and normalize line endings, then process with awk
sed 's/^\xEF\xBB\xBF//' | tr -d '\r' | awk '
# Check if line is a dash separator (only dashes and spaces, at least one dash)
function is_dash_line(line) {
    if (line == "" || line ~ /^[[:space:]]*$/) return 0
    return (line ~ /^[-[:space:]]+$/ && line ~ /-/)
}

# Parse column positions from dash line
# Sets global arrays: col_start[], col_end[], num_cols
function parse_columns(dash_line) {
    num_cols = 0
    n = length(dash_line)
    i = 1
    while (i <= n) {
        c = substr(dash_line, i, 1)
        if (c == "-") {
            start = i
            while (i <= n && substr(dash_line, i, 1) == "-") i++
            num_cols++
            col_start[num_cols] = start
            col_end[num_cols] = i - 1
        } else {
            i++
        }
    }
}

# Extract substring from line at column positions (1-indexed)
function extract_col(line, start, end) {
    if (start > length(line)) return ""
    len = end - start + 1
    if (start + len - 1 > length(line)) len = length(line) - start + 1
    return substr(line, start, len)
}

# Trim leading and trailing spaces
function trim(s) {
    gsub(/^[[:space:]]+/, "", s)
    gsub(/[[:space:]]+$/, "", s)
    return s
}

# Right trim only
function rtrim(s) {
    gsub(/[[:space:]]+$/, "", s)
    return s
}

# Check if line has spaces in all gap positions between columns
function valid_header_line(line) {
    for (c = 1; c < num_cols; c++) {
        gap_start = col_end[c] + 1
        gap_end = col_start[c + 1] - 1
        for (pos = gap_start; pos <= gap_end; pos++) {
            if (pos <= length(line)) {
                ch = substr(line, pos, 1)
                if (ch != " ") return 0
            }
        }
    }
    return 1
}

# Detect justification: "right" or "left"
function detect_justification(col_idx,    i, val, stripped, left_sp, right_sp) {
    for (i = 1; i <= num_data; i++) {
        val = data_raw[i, col_idx]
        stripped = trim(val)
        if (stripped == "") continue
        
        # Count leading spaces
        left_sp = 0
        while (left_sp < length(val) && substr(val, left_sp + 1, 1) == " ") left_sp++
        
        # Count trailing spaces
        right_sp = 0
        while (right_sp < length(val) && substr(val, length(val) - right_sp, 1) == " ") right_sp++
        
        if (left_sp > 0 && right_sp == 0) return "right"
        if (right_sp > 0 && left_sp == 0) return "left"
        if (left_sp > right_sp) return "right"
    }
    return "left"
}

# Pad string to width with spaces (left justify)
function ljust(s, width) {
    while (length(s) < width) s = s " "
    return s
}

# Pad string to width with spaces (right justify)
function rjust(s, width) {
    while (length(s) < width) s = " " s
    return s
}

# Process a detected table
function process_table() {
    # Calculate new widths
    for (c = 1; c <= num_cols; c++) {
        new_width[c] = 0
        # Max header line length for this column
        for (h = 1; h <= num_headers; h++) {
            len = length(trim(header_raw[h, c]))
            if (len > new_width[c]) new_width[c] = len
        }
        # Max data value length
        for (d = 1; d <= num_data; d++) {
            len = length(data_trimmed[d, c])
            if (len > new_width[c]) new_width[c] = len
        }
    }
    
    # Detect justification for each column
    for (c = 1; c <= num_cols; c++) {
        justify[c] = detect_justification(c)
    }
    
    # Output header lines (preserving multi-line structure)
    for (h = 1; h <= num_headers; h++) {
        out = ""
        for (c = 1; c <= num_cols; c++) {
            if (c > 1) out = out " "
            out = out ljust(trim(header_raw[h, c]), new_width[c])
        }
        print rtrim(out)
    }
    
    # Output dash line
    out = ""
    for (c = 1; c <= num_cols; c++) {
        if (c > 1) out = out " "
        dashes = ""
        for (i = 1; i <= new_width[c]; i++) dashes = dashes "-"
        out = out dashes
    }
    print out
    
    # Output data lines with preserved justification
    for (d = 1; d <= num_data; d++) {
        out = ""
        for (c = 1; c <= num_cols; c++) {
            if (c > 1) out = out " "
            if (justify[c] == "right") {
                out = out rjust(data_trimmed[d, c], new_width[c])
            } else {
                out = out ljust(data_trimmed[d, c], new_width[c])
            }
        }
        print rtrim(out)
    }
}

{
    # Store all lines
    NL++
    lines[NL] = $0
}

END {
    # Track which lines are consumed by tables
    for (i = 1; i <= NL; i++) consumed[i] = 0
    
    # Find tables and process them
    for (i = 1; i <= NL; i++) {
        if (consumed[i]) continue
        
        if (is_dash_line(lines[i])) {
            dash_idx = i
            parse_columns(lines[i])
            
            if (num_cols == 0) continue
            
            # Collect header lines going backwards
            num_headers = 0
            h = dash_idx - 1
            while (h >= 1 && !consumed[h]) {
                if (lines[h] ~ /^[[:space:]]*$/) break
                if (!valid_header_line(lines[h])) break
                num_headers++
                header_idx[num_headers] = h
                h--
            }
            
            if (num_headers == 0) continue
            
            # Reverse header indices to get top-to-bottom order
            for (j = 1; j <= int(num_headers / 2); j++) {
                tmp = header_idx[j]
                header_idx[j] = header_idx[num_headers - j + 1]
                header_idx[num_headers - j + 1] = tmp
            }
            
            # Extract header column values
            for (h = 1; h <= num_headers; h++) {
                for (c = 1; c <= num_cols; c++) {
                    header_raw[h, c] = extract_col(lines[header_idx[h]], col_start[c], col_end[c])
                }
            }
            
            # Collect data lines going forward
            num_data = 0
            d = dash_idx + 1
            while (d <= NL) {
                if (lines[d] ~ /^[[:space:]]*$/) break
                if (is_dash_line(lines[d])) break
                num_data++
                data_line_idx[num_data] = d
                for (c = 1; c <= num_cols; c++) {
                    data_raw[num_data, c] = extract_col(lines[d], col_start[c], col_end[c])
                    data_trimmed[num_data, c] = trim(data_raw[num_data, c])
                }
                d++
            }
            
            if (num_data == 0) continue
            
            # Mark lines as consumed
            for (h = 1; h <= num_headers; h++) consumed[header_idx[h]] = 1
            consumed[dash_idx] = 1
            for (d = 1; d <= num_data; d++) consumed[data_line_idx[d]] = 1
            
            # Store output position
            first_header = header_idx[1]
            table_output[first_header] = 1
            
            # Store table data for later output
            table_num_headers[first_header] = num_headers
            table_num_cols[first_header] = num_cols
            table_num_data[first_header] = num_data
            
            for (h = 1; h <= num_headers; h++) {
                for (c = 1; c <= num_cols; c++) {
                    table_header[first_header, h, c] = header_raw[h, c]
                }
            }
            for (c = 1; c <= num_cols; c++) {
                table_col_start[first_header, c] = col_start[c]
                table_col_end[first_header, c] = col_end[c]
            }
            for (d = 1; d <= num_data; d++) {
                for (c = 1; c <= num_cols; c++) {
                    table_data_raw[first_header, d, c] = data_raw[d, c]
                    table_data_trimmed[first_header, d, c] = data_trimmed[d, c]
                }
            }
        }
    }
    
    # Output all lines in order
    for (i = 1; i <= NL; i++) {
        if (table_output[i]) {
            # Restore table context and process
            num_headers = table_num_headers[i]
            num_cols = table_num_cols[i]
            num_data = table_num_data[i]
            
            for (h = 1; h <= num_headers; h++) {
                for (c = 1; c <= num_cols; c++) {
                    header_raw[h, c] = table_header[i, h, c]
                }
            }
            for (c = 1; c <= num_cols; c++) {
                col_start[c] = table_col_start[i, c]
                col_end[c] = table_col_end[i, c]
            }
            for (d = 1; d <= num_data; d++) {
                for (c = 1; c <= num_cols; c++) {
                    data_raw[d, c] = table_data_raw[i, d, c]
                    data_trimmed[d, c] = table_data_trimmed[i, d, c]
                }
            }
            
            process_table()
        } else if (!consumed[i]) {
            print lines[i]
        }
    }
}
'
