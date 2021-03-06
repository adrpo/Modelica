/* ModelicaIO.c - Array I/O functions

   Copyright (C) 2016-2017, Modelica Association and ESI ITI GmbH
   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
   ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
   FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
   CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
   OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/* The functions in this file are non-portable. The following #define's are used
   to define the system calls of the operating system

   NO_FILE_SYSTEM : A file system is not present (e.g. on dSPACE or xPC).
   NO_LOCALE      : locale.h is not present (e.g. on AVR).
   MODELICA_EXPORT: Prefix used for function calls. If not defined, blank is used
                    Useful definitions:
                    - "static" that is all functions become static
                      (useful if file is included with other C-sources for an
                       embedded system)
                    - "__declspec(dllexport)" if included in a DLL and the
                      functions shall be visible outside of the DLL

   Release Notes:
      Mar. 08, 2017: by Thomas Beutlich, ESI ITI GmbH
                     Added ModelicaIO_readRealTable from ModelicaStandardTables
                     (ticket #2192)

      Feb. 07, 2017: by Thomas Beutlich, ESI ITI GmbH
                     Added support for integer and single-precision variable
                     classes of MATLAB MAT-files (ticket #2106)

      Jan. 31, 2017: by Thomas Beutlich, ESI ITI GmbH
                     Added diagnostic message for (supported) partial read of table
                     from ASCII text file (ticket #2151)

      Jan. 07, 2017: by Thomas Beutlich, ESI ITI GmbH
                     Replaced strtok by re-entrant string tokenize function
                     (ticket #1153)

      Nov. 23, 2016: by Martin Sj�lund, SICS East Swedish ICT AB
                     Added NO_LOCALE define flag, in case the OS does
                     not have this (for example when using GCC compiler,
                     but not libc). Also added autoconf detection for
                     this flag, NO_PID, NO_TIME, and NO_FILE_SYSTEM

      Nov. 21, 2016: by Thomas Beutlich, ESI ITI GmbH
                     Fixed error handling if a variable cannot be found in a
                     MATLAB MAT-file (ticket #2119)

      Mar. 03, 2016: by Thomas Beutlich, ITI GmbH and Martin Otter, DLR
                     Implemented a first version (ticket #1856)
*/

#if !defined(MODELICA_EXPORT)
  #define MODELICA_EXPORT
#endif

#if defined(__gnu_linux__) && !defined(NO_FILE_SYSTEM)
#define _GNU_SOURCE 1
#endif

#include <stdlib.h>
#include <string.h>
#include "ModelicaIO.h"
#include "ModelicaUtilities.h"

#ifdef NO_FILE_SYSTEM
static void ModelicaNotExistError(const char* name) {
  /* Print error message if a function is not implemented */
    ModelicaFormatError("C-Function \"%s\" is called "
        "but is not implemented for the actual environment "
        "(e.g., because there is no file system available on the machine "
        "as for dSPACE or xPC systems)\n", name);
}

MODELICA_EXPORT void ModelicaIO_readMatrixSizes(_In_z_ const char* fileName,
    _In_z_ const char* matrixName, _Out_ int* dim) {
    ModelicaNotExistError("ModelicaIO_readMatrixSizes"); }
MODELICA_EXPORT void ModelicaIO_readRealMatrix(_In_z_ const char* fileName,
    _In_z_ const char* matrixName, _Out_ double* matrix, size_t m, size_t n,
    int verbose) {
    ModelicaNotExistError("ModelicaIO_readRealMatrix"); }
MODELICA_EXPORT int ModelicaIO_writeRealMatrix(_In_z_ const char* fileName,
    _In_z_ const char* matrixName, _In_ double* matrix, size_t m, size_t n,
    int append, _In_z_ const char* version) {
    ModelicaNotExistError("ModelicaIO_writeRealMatrix"); return 0; }
MODELICA_EXPORT double* ModelicaIO_readRealTable(_In_z_ const char* fileName,
    _In_z_ const char* matrixName, _Out_ size_t* m, _Out_ size_t* n,
    int verbose) {
    ModelicaNotExistError("ModelicaIO_readRealTable"); return NULL; }
#else

#include <stdio.h>
#if !defined(NO_LOCALE)
#include <locale.h>
#endif
#include "ModelicaMatIO.h"

/* The standard way to detect posix is to check _POSIX_VERSION,
 * which is defined in <unistd.h>
 */
#if defined(__unix__) || defined(__linux__) || defined(__APPLE_CC__)
#include <unistd.h>
#endif
#if !defined(_POSIX_) && defined(_POSIX_VERSION)
#define _POSIX_ 1
#endif

/* Use re-entrant string tokenize function if available */
#if defined(_POSIX_)
#elif defined(_MSC_VER) && _MSC_VER >= 1400
#define strtok_r(str, delim, saveptr) strtok_s((str), (delim), (saveptr))
#else
#define strtok_r(str, delim, saveptr) strtok((str), (delim))
#endif

#if !defined(LINE_BUFFER_LENGTH)
#define LINE_BUFFER_LENGTH (64)
#endif
#if !defined(MATLAB_NAME_LENGTH_MAX)
#define MATLAB_NAME_LENGTH_MAX (64)
#endif

typedef struct MatIO {
    mat_t* mat; /* Pointer to MAT-file */
    matvar_t* matvar; /* Pointer to MAT-file variable for data */
    matvar_t* matvarRoot; /* Pointer to MAT-file variable for free */
} MatIO;

static double* readMatTable(_In_z_ const char* tableName, _In_z_ const char* fileName,
                            _Out_ size_t* m, _Out_ size_t* n) MODELICA_NONNULLATTR;
  /* Read a table from a MATLAB MAT-file using MatIO functions

     <- RETURN: Pointer to array (row-wise storage) of table values
  */

static void readMatIO(_In_z_ const char* fileName, _In_z_ const char* matrixName, _Inout_ MatIO* matio);
  /* Read a variable from a MATLAB MAT-file using MatIO functions */

static double* readTxtTable(_In_z_ const char* tableName, _In_z_ const char* fileName,
                            _Out_ size_t* m, _Out_ size_t* n) MODELICA_NONNULLATTR;
  /* Read a table from an ASCII text file

     <- RETURN: Pointer to array (row-wise storage) of table values
  */

static int readLine(_In_ char** buf, _In_ int* bufLen, _In_ FILE* fp) MODELICA_NONNULLATTR;
  /* Read line (of unknown and arbitrary length) from an ASCII text file */

static int IsNumber(char* token);
  /*  Check, whether a token represents a floating-point number */

static void transpose(_Inout_ double* table, size_t nRow, size_t nCol) MODELICA_NONNULLATTR;
  /* Cycle-based in-place array transposition */

MODELICA_EXPORT void ModelicaIO_readMatrixSizes(_In_z_ const char* fileName,
                                _In_z_ const char* matrixName,
                                _Out_ int* dim) {
    MatIO matio = {NULL, NULL, NULL};

    dim[0] = 0;
    dim[1] = 0;

    readMatIO(fileName, matrixName, &matio);
    if (NULL != matio.matvar) {
        matvar_t* matvar = matio.matvar;

        dim[0] = (int)matvar->dims[0];
        dim[1] = (int)matvar->dims[1];
    }

    Mat_VarFree(matio.matvarRoot);
    (void)Mat_Close(matio.mat);
}

MODELICA_EXPORT void ModelicaIO_readRealMatrix(_In_z_ const char* fileName,
                               _In_z_ const char* matrixName,
                               _Out_ double* matrix, size_t m, size_t n,
                               int verbose) {
    MatIO matio = {NULL, NULL, NULL};
    int tableReadError = 0;

    if (verbose == 1) {
        /* Print info message, that matrix / file is loading */
        ModelicaFormatMessage("... loading \"%s\" from \"%s\"\n",
            matrixName, fileName);
    }

    readMatIO(fileName, matrixName, &matio);
    if (NULL != matio.matvar) {
        matvar_t* matvar = matio.matvar;

        /* Check if number of rows matches */
        if (m != matvar->dims[0]) {
            Mat_VarFree(matio.matvarRoot);
            (void)Mat_Close(matio.mat);
            ModelicaFormatError(
                "Cannot read %lu rows of array \"%s(%lu,%lu)\" "
                "from file \"%s\"\n", (unsigned long)m, matrixName,
                (unsigned long)matvar->dims[0], (unsigned long)matvar->dims[1],
                fileName);
            return;
        }

        /* Check if number of columns matches */
        if (n != matvar->dims[1]) {
            Mat_VarFree(matio.matvarRoot);
            (void)Mat_Close(matio.mat);
            ModelicaFormatError(
                "Cannot read %lu columns of array \"%s(%lu,%lu)\" "
                "from file \"%s\"\n", (unsigned long)n, matrixName,
                (unsigned long)matvar->dims[0], (unsigned long)matvar->dims[1],
                fileName);
            return;
        }

        {
            int start[2] = {0, 0};
            int stride[2] = {1, 1};
            int edge[2];
            edge[0] = (int)matvar->dims[0];
            edge[1] = (int)matvar->dims[1];
            tableReadError = Mat_VarReadData(matio.mat, matvar, matrix, start, stride, edge);
        }
    }

    Mat_VarFree(matio.matvarRoot);
    (void)Mat_Close(matio.mat);

    if (tableReadError == 0 && NULL != matrix) {
        /* Array is stored column-wise -> need to transpose */
        transpose(matrix, m, n);
    }
    else {
        ModelicaFormatError(
            "Error when reading numeric data of matrix \"%s(%lu,%lu)\" "
            "from file \"%s\"\n", matrixName, (unsigned long)m,
            (unsigned long)n, fileName);
    }
}

MODELICA_EXPORT int ModelicaIO_writeRealMatrix(_In_z_ const char* fileName,
                               _In_z_ const char* matrixName,
                               _In_ double* matrix, size_t m, size_t n,
                               int append,
                               _In_z_ const char* version) {
    int status;
    mat_t* mat;
    matvar_t* matvar;
    size_t dims[2];
    double* aT;
    enum mat_ft matv;
    enum matio_compression matc;

    if ((0 != strcmp(version, "4")) && (0 != strcmp(version, "6")) && (0 != strcmp(version, "7")) && (0 != strcmp(version, "7.3"))) {
        ModelicaFormatError("Invalid version %s for file \"%s\"\n", version, fileName);
        return 0;
    }
    if (0 == strcmp(version, "4")) {
        matv = MAT_FT_MAT4;
        matc = MAT_COMPRESSION_NONE;
    }
    else if (0 == strcmp(version, "7.3")) {
        matv = MAT_FT_MAT73;
        matc = MAT_COMPRESSION_ZLIB;
    }
    else if (0 == strcmp(version, "7")) {
        matv = MAT_FT_MAT5;
        matc = MAT_COMPRESSION_ZLIB;
    }
    else {
        matv = MAT_FT_MAT5;
        matc = MAT_COMPRESSION_NONE;
    }

    if (append == 0) {
        mat = Mat_CreateVer(fileName, NULL, matv);
        if (mat == NULL) {
            ModelicaFormatError("Not possible to newly create file \"%s\"\n(maybe version 7.3 not supported)\n", fileName);
            return 0;
        }
    } else {
        mat = Mat_Open(fileName, (int)MAT_ACC_RDWR | matv);
        if (mat == NULL) {
            ModelicaFormatError("Not possible to open file \"%s\"\n", fileName);
            return 0;
        }
    }

    /* MAT file array is stored column-wise -> need to transpose */
    aT = (double*)malloc(m*n*sizeof(double));
    if (aT == NULL) {
        (void)Mat_Close(mat);
        ModelicaError("Memory allocation error\n");
        return 0;
    }
    memcpy(aT, matrix, m*n*sizeof(double));
    transpose(aT, n, m);

    if (append != 0) {
        (void)Mat_VarDelete(mat, matrixName);
    }

    dims[0] = m;
    dims[1] = n;
    matvar = Mat_VarCreate(matrixName, MAT_C_DOUBLE, MAT_T_DOUBLE, 2, dims, aT, MAT_F_DONT_COPY_DATA);
    status = Mat_VarWrite(mat, matvar, matc);
    Mat_VarFree(matvar);
    (void)Mat_Close(mat);
    free(aT);
    if (status != 0) {
        ModelicaFormatError("Cannot write variable \"%s\" to \"%s\"\n", matrixName, fileName);
        return 0;
    }
    return 1;
}

MODELICA_EXPORT double* ModelicaIO_readRealTable(_In_z_ const char* fileName,
                                 _In_z_ const char* tableName,
                                 _Out_ size_t* m, _Out_ size_t* n,
                                 int verbose) {
    double* table = NULL;
    const char* ext;
    int isMatExt = 0;

    /* Table file can be either ASCII text or binary MATLAB MAT-file */
    ext = strrchr(fileName, '.');
    if (ext != NULL) {
        if (0 == strncmp(ext, ".mat", 4) ||
            0 == strncmp(ext, ".MAT", 4)) {
            isMatExt = 1;
        }
    }

    if (verbose == 1) {
        /* Print info message, that table / file is loading */
        ModelicaFormatMessage("... loading \"%s\" from \"%s\"\n",
            tableName, fileName);
    }

    if (isMatExt == 1) {
        table = readMatTable(fileName, tableName, m, n);
    }
    else {
        table = readTxtTable(fileName, tableName, m, n);
    }
    return table;
}

static double* readMatTable(_In_z_ const char* tableName, _In_z_ const char* fileName,
                            _Out_ size_t* m, _Out_ size_t* n) {
    double* table = NULL;
    MatIO matio = {NULL, NULL, NULL};
    int tableReadError = 0;

    *m = 0;
    *n = 0;

    readMatIO(fileName, tableName, &matio);
    if (NULL != matio.matvar) {
        matvar_t* matvar = matio.matvar;

        table = (double*)malloc(matvar->dims[0]*matvar->dims[1]*sizeof(double));
        if (table == NULL) {
            Mat_VarFree(matio.matvarRoot);
            (void)Mat_Close(matio.mat);
            ModelicaError("Memory allocation error\n");
            return NULL;
        }

        {
            int start[2] = {0, 0};
            int stride[2] = {1, 1};
            int edge[2];
            edge[0] = (int)matvar->dims[0];
            edge[1] = (int)matvar->dims[1];
            tableReadError = Mat_VarReadData(matio.mat, matvar, table, start, stride, edge);
            *m = matvar->dims[0];
            *n = matvar->dims[1];
        }
    }

    Mat_VarFree(matio.matvarRoot);
    (void)Mat_Close(matio.mat);

    if (tableReadError == 0 && NULL != table) {
        /* Array is stored column-wise -> need to transpose */
        transpose(table, *m, *n);
    }
    else {
        size_t dim[2];

        dim[0] = *m;
        dim[1] = *n;
        *m = 0;
        *n = 0;
        free(table);
        table = NULL;
        ModelicaFormatError(
            "Error when reading numeric data of matrix \"%s(%lu,%lu)\" "
            "from file \"%s\"\n", tableName, (unsigned long)dim[0],
            (unsigned long)dim[1], fileName);
    }
    return table;
}

static void readMatIO(_In_z_ const char* fileName, _In_z_ const char* matrixName, _Inout_ MatIO* matio) {
    mat_t* mat;
    matvar_t* matvar;
    matvar_t* matvarRoot;
    char* matrixNameCopy;
    char* token;
#if defined(_POSIX_) || (defined(_MSC_VER) && _MSC_VER >= 1400)
    char* nextToken = NULL;
#endif

    matrixNameCopy = (char*)malloc((strlen(matrixName) + 1)*sizeof(char));
    if (matrixNameCopy != NULL) {
        strcpy(matrixNameCopy, matrixName);
    }
    else {
        ModelicaError("Memory allocation error\n");
        return;
    }

    mat = Mat_Open(fileName, (int)MAT_ACC_RDONLY);
    if (mat == NULL) {
        free(matrixNameCopy);
        ModelicaFormatError("Not possible to open file \"%s\": "
            "No such file or directory\n", fileName);
        return;
    }

    token = strtok_r(matrixNameCopy, ".", &nextToken);
    matvarRoot = Mat_VarReadInfo(mat, token == NULL ? matrixName : token);
    if (matvarRoot == NULL) {
        (void)Mat_Close(mat);
        if (token == NULL) {
            free(matrixNameCopy);
            ModelicaFormatError(
                "Variable \"%s\" not found on file \"%s\".\n",
                matrixName, fileName);
        }
        else {
            char matrixNameBuf[MATLAB_NAME_LENGTH_MAX];
            if (strlen(token) > MATLAB_NAME_LENGTH_MAX - 1) {
                strncpy(matrixNameBuf, token, MATLAB_NAME_LENGTH_MAX - 1);
                matrixNameBuf[MATLAB_NAME_LENGTH_MAX - 1] = '\0';
                free(matrixNameCopy);
                ModelicaFormatError(
                    "Variable \"%s...\" not found on file \"%s\".\n",
                    matrixNameBuf, fileName);
            }
            else {
                strcpy(matrixNameBuf, token);
                free(matrixNameCopy);
                ModelicaFormatError(
                    "Variable \"%s\" not found on file \"%s\".\n",
                    matrixNameBuf, fileName);
            }
        }
        return;
    }

    matvar = matvarRoot;
    token = strtok_r(NULL, ".", &nextToken);
    /* Get field while matvar is of struct class and of 1x1 size */
    while (token != NULL && matvar != NULL) {
        if (matvar->class_type == MAT_C_STRUCT && matvar->rank == 2 &&
            matvar->dims[0] == 1 && matvar->dims[1] == 1) {
            matvar = Mat_VarGetStructField(matvar, (void*)token, MAT_BY_NAME, 0);
            token = strtok_r(NULL, ".", &nextToken);
        }
        else {
            matvar = NULL;
            break;
        }
    }
    free(matrixNameCopy);

    if (matvar == NULL) {
        Mat_VarFree(matvarRoot);
        (void)Mat_Close(mat);
        ModelicaFormatError(
            "Variable \"%s\" not found on file \"%s\".\n", matrixName, fileName);
        return;
    }

    /* Check if matvar is a matrix */
    if (matvar->rank != 2) {
        Mat_VarFree(matvarRoot);
        (void)Mat_Close(mat);
        ModelicaFormatError(
            "Variable \"%s\" has not the required rank 2.\n", matrixName);
        return;
    }

    /* Check if variable class of matvar is numeric (and thus non-sparse) */
    if (matvar->class_type != MAT_C_DOUBLE && matvar->class_type != MAT_C_SINGLE &&
        matvar->class_type != MAT_C_INT8 && matvar->class_type != MAT_C_UINT8 &&
        matvar->class_type != MAT_C_INT16 && matvar->class_type != MAT_C_UINT16 &&
        matvar->class_type != MAT_C_INT32 && matvar->class_type != MAT_C_UINT32 &&
        matvar->class_type != MAT_C_INT64 && matvar->class_type != MAT_C_UINT64) {
        Mat_VarFree(matvarRoot);
        (void)Mat_Close(mat);
        ModelicaFormatError("Matrix \"%s\" has not the required "
            "numeric variable class.\n", matrixName);
        return;
    }
    matvar->class_type = MAT_C_DOUBLE;

    /* Check if matvar is purely real-valued */
    if (matvar->isComplex) {
        Mat_VarFree(matvarRoot);
        (void)Mat_Close(mat);
        ModelicaFormatError("Matrix \"%s\" must not be complex.\n",
            matrixName);
        return;
    }

    /* Set output fields for MatIO structure */
    matio->mat = mat;
    matio->matvar = matvar;
    matio->matvarRoot = matvarRoot;
}

static int IsNumber(char* token) {
    int foundExponentSign = 0;
    int foundExponent = 0;
    int foundDec = 0;
    int isNumber = 1;
    int k = 0;

    if (token[0] == '-' || token[0] == '+') {
        k = 1;
    }
    else {
        k = 0;
    }
    while (token[k] != '\0') {
        if (token[k] >= '0' && token[k] <= '9') {
            k++;
        }
        else if (token[k] == '.' && foundDec == 0 &&
            foundExponent == 0 && foundExponentSign == 0) {
            foundDec = 1;
            k++;
        }
        else if ((token[k] == 'e' || token[k] == 'E') &&
            foundExponent == 0) {
            foundExponent = 1;
            k++;
        }
        else if ((token[k] == '-' || token[k] == '+') &&
            foundExponent == 1 && foundExponentSign == 0) {
            foundExponentSign = 1;
            k++;
        }
        else {
            isNumber = 0;
            break;
        }
    }
    return isNumber;
}

static double* readTxtTable(_In_z_ const char* tableName, _In_z_ const char* fileName,
                            _Out_ size_t* m, _Out_ size_t* n) {
#define DELIM_TABLE_HEADER " \t(,)\r"
#define DELIM_TABLE_NUMBER " \t,;\r"
    double* table = NULL;
    char* buf;
    int bufLen = LINE_BUFFER_LENGTH;
    FILE* fp;
    int foundTable = 0;
    int tableReadError;
    unsigned long nRow = 0;
    unsigned long nCol = 0;
    unsigned long lineNo = 1;
#if defined(NO_LOCALE)
    const char * const dec = ".";
#elif defined(_MSC_VER) && _MSC_VER >= 1400
    _locale_t loc;
#elif defined(__GLIBC__) && defined(__GLIBC_MINOR__) && ((__GLIBC__ << 16) + __GLIBC_MINOR__ >= (2 << 16) + 3)
    locale_t loc;
#else
    char* dec;
#endif

    fp = fopen(fileName, "r");
    if (fp == NULL) {
        ModelicaFormatError("Not possible to open file \"%s\": "
            "No such file or directory\n", fileName);
        return NULL;
    }

    buf = (char*)malloc(LINE_BUFFER_LENGTH*sizeof(char));
    if (buf == NULL) {
        fclose(fp);
        ModelicaError("Memory allocation error\n");
        return NULL;
    }

    /* Read file header */
    if ((tableReadError = readLine(&buf, &bufLen, fp)) != 0) {
        free(buf);
        fclose(fp);
        if (tableReadError < 0) {
            ModelicaFormatError(
                "Error reading first line from file \"%s\": "
                "End-Of-File reached.\n", fileName);
        }
        return NULL;
    }

    /* Expected file header format: "#1" */
    if (0 != strncmp(buf, "#1", 2)) {
        size_t len = strlen(buf);
        fclose(fp);
        if (len == 0) {
            free(buf);
            ModelicaFormatError(
                "Error reading format and version information in first "
                "line of file \"%s\": \"#1\" expected.\n", fileName);
        }
        else if (len == 1) {
            char c0 = buf[0];
            free(buf);
            ModelicaFormatError(
                "Error reading format and version information in first "
                "line of file \"%s\": \"#1\" expected, but \"%c\" found.\n",
                fileName, c0);
        }
        else {
            char c0 = buf[0];
            char c1 = buf[1];
            free(buf);
            ModelicaFormatError(
                "Error reading format and version information in first "
                "line of file \"%s\": \"#1\" expected, but \"%c%c\" "
                "found.\n", fileName, c0, c1);
        }
        return NULL;
    }

#if defined(NO_LOCALE)
#elif defined(_MSC_VER) && _MSC_VER >= 1400
    loc = _create_locale(LC_NUMERIC, "C");
#elif defined(__GLIBC__) && defined(__GLIBC_MINOR__) && ((__GLIBC__ << 16) + __GLIBC_MINOR__ >= (2 << 16) + 3)
    loc = newlocale(LC_NUMERIC, "C", NULL);
#else
    dec = localeconv()->decimal_point;
#endif

    /* Loop over lines of file */
    while (readLine(&buf, &bufLen, fp) == 0) {
        char* token;
        char* endptr;
#if defined(_POSIX_) || (defined(_MSC_VER) && _MSC_VER >= 1400)
        char* nextToken = NULL;
#endif

        lineNo++;
        /* Expected table header format: "dataType tableName(nRow,nCol)" */
        token = strtok_r(buf, DELIM_TABLE_HEADER, &nextToken);
        if (token == NULL) {
            continue;
        }
        if ((0 != strcmp(token, "double")) && (0 != strcmp(token, "float"))) {
            continue;
        }
        token = strtok_r(NULL, DELIM_TABLE_HEADER, &nextToken);
        if (token == NULL) {
            continue;
        }
        if (0 == strcmp(token, tableName)) {
            foundTable = 1;
        }
        else {
            continue;
        }
        token = strtok_r(NULL, DELIM_TABLE_HEADER, &nextToken);
        if (token == NULL) {
            continue;
        }
#if !defined(NO_LOCALE) && (defined(_MSC_VER) && _MSC_VER >= 1400)
        nRow = (unsigned long)_strtol_l(token, &endptr, 10, loc);
#elif !defined(NO_LOCALE) && (defined(__GLIBC__) && defined(__GLIBC_MINOR__) && ((__GLIBC__ << 16) + __GLIBC_MINOR__ >= (2 << 16) + 3))
        nRow = (unsigned long)strtol_l(token, &endptr, 10, loc);
#else
        nRow = (unsigned long)strtol(token, &endptr, 10);
#endif
        if (*endptr != 0) {
            continue;
        }
        token = strtok_r(NULL, DELIM_TABLE_HEADER, &nextToken);
        if (token == NULL) {
            continue;
        }
#if !defined(NO_LOCALE) && (defined(_MSC_VER) && _MSC_VER >= 1400)
        nCol = (unsigned long)_strtol_l(token, &endptr, 10, loc);
#elif !defined(NO_LOCALE) && (defined(__GLIBC__) && defined(__GLIBC_MINOR__) && ((__GLIBC__ << 16) + __GLIBC_MINOR__ >= (2 << 16) + 3))
        nCol = (unsigned long)strtol_l(token, &endptr, 10, loc);
#else
        nCol = (unsigned long)strtol(token, &endptr, 10);
#endif
        if (*endptr != 0) {
            continue;
        }

        { /* foundTable == 1 */
            size_t i = 0;
            size_t j = 0;

            table = (double*)malloc(nRow*nCol*sizeof(double));
            if (table == NULL) {
                *m = 0;
                *n = 0;
                free(buf);
                fclose(fp);
#if defined(NO_LOCALE)
#elif defined(_MSC_VER) && _MSC_VER >= 1400
                _free_locale(loc);
#elif defined(__GLIBC__) && defined(__GLIBC_MINOR__) && ((__GLIBC__ << 16) + __GLIBC_MINOR__ >= (2 << 16) + 3)
                freelocale(loc);
#endif
                ModelicaError("Memory allocation error\n");
                return table;
            }

            /* Loop over rows and store table row-wise */
            while (tableReadError == 0 && i < nRow) {
                int k = 0;

                lineNo++;
                if ((tableReadError = readLine(&buf, &bufLen, fp)) != 0) {
                    break;
                }
                /* Ignore leading white space */
                while (k < bufLen - 1) {
                    if (buf[k] != ' ' && buf[k] != '\t') {
                        break;
                    }
                    k++;
                }
                if (buf[k] == '\0' || buf[k] == '#') {
                    /* Skip empty or comment line */
                    continue;
                }
#if defined(_POSIX_) || (defined(_MSC_VER) && _MSC_VER >= 1400)
                nextToken = NULL;
#endif
                token = strtok_r(&buf[k], DELIM_TABLE_NUMBER, &nextToken);
                while (token != NULL && i < nRow && j < nCol) {
                    if (token[0] == '#') {
                        /* Skip trailing comment line */
                        break;
                    }
#if !defined(NO_LOCALE) && (defined(_MSC_VER) && _MSC_VER >= 1400)
                    table[i*nCol + j] = _strtod_l(token, &endptr, loc);
                    if (*endptr != 0) {
                        tableReadError = 1;
                    }
#elif !defined(NO_LOCALE) && (defined(__GLIBC__) && defined(__GLIBC_MINOR__) && ((__GLIBC__ << 16) + __GLIBC_MINOR__ >= (2 << 16) + 3))
                    table[i*nCol + j] = strtod_l(token, &endptr, loc);
                    if (*endptr != 0) {
                        tableReadError = 1;
                    }
#else
                    if (*dec == '.') {
                        table[i*nCol + j] = strtod(token, &endptr);
                    }
                    else if (NULL == strchr(token, '.')) {
                        table[i*nCol + j] = strtod(token, &endptr);
                    }
                    else {
                        char* token2 = (char*)malloc(
                            (strlen(token) + 1)*sizeof(char));
                        if (token2 != NULL) {
                            char* p;
                            strcpy(token2, token);
                            p = strchr(token2, '.');
                            *p = *dec;
                            table[i*nCol + j] = strtod(token2, &endptr);
                            if (*endptr != 0) {
                                tableReadError = 1;
                            }
                            free(token2);
                        }
                        else {
                            *m = 0;
                            *n = 0;
                            free(buf);
                            fclose(fp);
                            tableReadError = 1;
                            ModelicaError("Memory allocation error\n");
                            break;
                        }
                    }
#endif
                    if (++j == nCol) {
                        i++; /* Increment row index */
                        j = 0; /* Reset column index */
                    }
                    if (tableReadError == 0) {
                        token = strtok_r(NULL, DELIM_TABLE_NUMBER, &nextToken);
                        continue;
                    }
                    else {
                        break;
                    }
                }
                /* Check for trailing non-comment character */
                if (token != NULL && token[0] != '#') {
                    tableReadError = 1;
                    /* Check for trailing number (on same line) */
                    if (i == nRow && 1 == IsNumber(token)) {
                        tableReadError = 2;
                    }
                    break;
                }
                /* Extra check for partial table read */
                else if (NULL == token && 0 == tableReadError && i == nRow) {
                    unsigned long lineNoPartial = lineNo;
                    int tableReadPartial = 0;
                    while (readLine(&buf, &bufLen, fp) == 0) {
                        lineNoPartial++;
                        /* Ignore leading white space */
                        while (k < bufLen - 1) {
                            if (buf[k] != ' ' && buf[k] != '\t') {
                                break;
                            }
                            k++;
                        }
                        if (buf[k] == '\0' || buf[k] == '#') {
                            /* Skip empty or comment line */
                            continue;
                        }
#if defined(_POSIX_) || (defined(_MSC_VER) && _MSC_VER >= 1400)
                        nextToken = NULL;
#endif
                        token = strtok_r(&buf[k], DELIM_TABLE_NUMBER, &nextToken);
                        if (NULL != token) {
                            if (1 == IsNumber(token)) {
                                tableReadPartial = 1;
                            }
                            /* Else, it is not a number: No further check
                               is performed, if legal or not
                            */
                        }
                        break;
                    }
                    if (1 == tableReadPartial) {
                        ModelicaFormatMessage(
                            "The table dimensions of matrix \"%s(%lu,%lu)\" from file "
                            "\"%s\" do not match the actual table size (line %lu).\n",
                            tableName, nRow, nCol, fileName, lineNoPartial);
                    }
                    break;
                }
            }
            break;
        }
    }

    free(buf);
    fclose(fp);
#if defined(NO_LOCALE)
#elif defined(_MSC_VER) && _MSC_VER >= 1400
    _free_locale(loc);
#elif defined(__GLIBC__) && defined(__GLIBC_MINOR__) && ((__GLIBC__ << 16) + __GLIBC_MINOR__ >= (2 << 16) + 3)
    freelocale(loc);
#endif
    if (foundTable == 0) {
        ModelicaFormatError(
            "Table matrix \"%s\" not found on file \"%s\".\n",
            tableName, fileName);
        return table;
    }

    if (tableReadError == 0) {
        *m = (size_t)nRow;
        *n = (size_t)nCol;
    }
    else {
        free(table);
        table = NULL;
        *m = 0;
        *n = 0;
        if (tableReadError == EOF) {
            ModelicaFormatError(
                "End-of-file reached when reading numeric data of matrix "
                "\"%s(%lu,%lu)\" from file \"%s\"\n", tableName, nRow,
                nCol, fileName);
        }
        else if (tableReadError == 2) {
            ModelicaFormatError(
                "The table dimensions of matrix \"%s(%lu,%lu)\" from file "
                "\"%s\" do not match the actual table size (line %lu).\n",
                tableName, nRow, nCol, fileName, lineNo);
        }
        else {
            ModelicaFormatError(
                "Error in line %lu when reading numeric data of matrix "
                "\"%s(%lu,%lu)\" from file \"%s\"\n", lineNo, tableName,
                nRow, nCol, fileName);
        }
    }
    return table;
#undef DELIM_TABLE_HEADER
#undef DELIM_TABLE_NUMBER
}

static int readLine(_In_ char** buf, _In_ int* bufLen, _In_ FILE* fp) {
    char* offset;
    int oldBufLen;

    if (fgets(*buf, *bufLen, fp) == NULL) {
        return EOF;
    }

    do {
        char* p;
        char* tmp;

        if ((p = strchr(*buf, '\n')) != NULL) {
            *p = '\0';
            return 0;
        }

        oldBufLen = *bufLen;
        *bufLen *= 2;
        tmp = (char*)realloc(*buf, (size_t)*bufLen);
        if (tmp == NULL) {
            fclose(fp);
            free(*buf);
            ModelicaError("Memory allocation error\n");
            return 1;
        }
        *buf = tmp;
        offset = &((*buf)[oldBufLen - 1]);

    } while (fgets(offset, oldBufLen + 1, fp));

    return 0;
}

static void transpose(_Inout_ double* table, size_t nRow, size_t nCol) {
  /* Reference:

     Cycle-based in-place array transposition
     (http://en.wikipedia.org/wiki/In-place_matrix_transposition#Non-square_matrices:_Following_the_cycles)
  */

    size_t i;
    for (i = 1; i < nRow*nCol - 1; i++) {
        size_t x = nRow*(i % nCol) + i/nCol; /* predecessor of i in the cycle */
        /* Continue if cycle is of length one or predecessor already was visited */
        if (x <= i) {
            continue;
        }
        /* Continue if cycle already was visited */
        while (x > i) {
            x = nRow*(x % nCol) + x/nCol;
        }
        if (x < i) {
            continue;
        }
        {
            double tmp = table[i];
            size_t s = i; /* start index in the cycle */
            x = nRow*(i % nCol) + i/nCol; /* predecessor of i in the cycle */
            while (x != i) {
                table[s] = table[x];
                s = x;
                x = nRow*(x % nCol) + x/nCol;
            }
            table[s] = tmp;
        }
    }
}
#endif
