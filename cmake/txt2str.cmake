# https://gist.github.com/sivachandran/3a0de157dccef822a230
include(CMakeParseArguments)

# Script to wrap opencl text with raw string delimiters and declare static char pointer. 
# Parameters
#   SOURCE_FILE     - The path of source file whose contents will be embedded in the header file.
#   VARIABLE_NAME   - The name of the variable for the string constant.
#   HEADER_FILE     - The path of header file.

set(oneValueArgs SOURCE_FILE VARIABLE_NAME HEADER_FILE)

# Preserve the existing leading and trailing newlines.
file(READ "${TXT2STR_SOURCE_FILE}" asciiString)
set(asciiString "\n\n${asciiString}\n\n")

# MSVC limits each string literal to 16,380 characters before concatenation.
string(LENGTH "${asciiString}" totalLength)
set(offset 0)
file(WRITE "${TXT2STR_HEADER_FILE}"
    "static const char* ${TXT2STR_VARIABLE_NAME} =\n")
while(offset LESS totalLength)
    string(SUBSTRING "${asciiString}" ${offset} 8000 chunk)
    file(APPEND "${TXT2STR_HEADER_FILE}" "R\"delim(${chunk})delim\"\n")
    math(EXPR offset "${offset} + 8000")
endwhile()
file(APPEND "${TXT2STR_HEADER_FILE}" ";\n")
