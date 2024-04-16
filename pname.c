#include <ctype.h>
#include <stdbool.h>
#include <string.h>
#include <regex.h>

#include "postgres.h"
#include "fmgr.h"
#include "utils/builtins.h"
#include "catalog/pg_collation.h"
#include "common/hashfn.h"
#include "utils/builtins.h"
#include "utils/formatting.h"

#ifdef PG_MODULE_MAGIC
PG_MODULE_MAGIC;
#endif

typedef struct PersonName {
    int32 length;
    char fgname[FLEXIBLE_ARRAY_MEMBER];
} PersonName;

/*****************************************************************************
 * Input/Output functions
 *****************************************************************************/
void errorLog(char *str);
bool checkFamilyName(char *familyName);
bool checkGivenName(char *givenName);
bool checkGivenSpace(char **givenName);

bool checkGivenSpace(char **givenName) {
    if (**givenName == ' ') {
        (*givenName)++;
    }
    if (**givenName == ' ' || **givenName == '\0') {
        return false;
    }
    return true;
}

bool checkFamilyName(char *familyName) {
    regex_t regex;
    int ret;
    char * familyRegex = "^[A-Z][a-zA-Z'-]{1,}( [A-Z][a-zA-Z'-]{1,})*$";
    ret = regcomp(&regex, familyRegex, REG_EXTENDED);
    if (ret != 0) {
        return false;
    }
    ret = regexec(&regex, familyName, 0, NULL, 0);
    regfree(&regex);

    if (ret == 0) {
        return true;
    } else {
        return false;
    }
}

bool checkGivenName(char *givenName) {
    regex_t regex;
    int ret;
    char * givenRegex = "^ ?[A-Z][a-zA-Z'-]{1,}( [A-Z][a-zA-Z'-]{1,})*$";
    ret = regcomp(&regex, givenRegex, REG_EXTENDED);
    if (ret != 0) {
        return false;
    }

    ret = regexec(&regex, givenName, 0, NULL, 0);
    regfree(&regex);

    if (ret == 0) {
        return true;
    } else {
        return false;
    }
}

void errorLog(char *str) {
    ereport(ERROR,
            (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
             errmsg("invalid input syntax for type %s: \"%s\"", "PersonName", str)));
}

PG_FUNCTION_INFO_V1(personname_in);

Datum personname_in(PG_FUNCTION_ARGS) {
    char *str = PG_GETARG_CSTRING(0);
    char *familyName;
    char *givenName;
    PersonName *result;
    int length;
    int commaCount = 0;
    char *original_str = strdup(str);

    for (int i = 0; str[i] != '\0'; i++) {
        if (str[i] == ',') {
            commaCount++;
        }
    }

    if (commaCount != 1) {
        errorLog(original_str);
    }

    familyName = strtok(str, ",");
    givenName = strtok(NULL, ",");
    // check empty
    if (familyName == NULL || givenName == NULL) {
        errorLog(original_str);
    }
    // check familyName letter length
    if (!checkFamilyName(familyName)) {
        errorLog(original_str);
    }
    // remove space
    if (!checkGivenSpace(&givenName)) {
        errorLog(original_str);
    }

    if (!checkGivenName(givenName)) {
        errorLog(original_str);
    }
    length = strlen(familyName) + strlen(givenName) + 1;

    result = (PersonName *) palloc(length + 1 + VARHDRSZ);

    SET_VARSIZE(result, length + 1 + VARHDRSZ);

    snprintf(result->fgname, length + 1, "%s,%s", familyName, givenName);

    PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(personname_out);

Datum personname_out(PG_FUNCTION_ARGS) {
    PersonName *pname = (PersonName *)PG_GETARG_POINTER(0);
    char *result;
    int32 input_len = VARSIZE(pname) - VARHDRSZ;
    result = (char *)palloc(input_len + 1);
    memcpy(result, pname->fgname, input_len);
    result[input_len] = '\0';
    PG_RETURN_CSTRING(result);
}

/*****************************************************************************
 * Operator class for defining B-tree index
 *
 *****************************************************************************/
static char *extract_given_name(PersonName *pname) {
    int given_len;
    char *comma_position;
    char *given_name;
    comma_position = strchr(pname->fgname, ',');
    if (comma_position == NULL) {
        ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("Invalid person name format")));
    }
    given_len = strlen(comma_position + 1);
    given_name = (char *) palloc(given_len + 1);
    memcpy(given_name, comma_position + 1, given_len);
    given_name[given_len] = '\0';
    return given_name;
}

static char *extract_family_name(PersonName *pname) {
    char *comma_position;
    int family_len;
    char *family_name;
    comma_position = strchr(pname->fgname, ',');
    if (comma_position == NULL) {
        ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("Invalid person name format")));
    }
    family_len = comma_position - pname->fgname;
    family_name = (char *) palloc(family_len + 1);
    memcpy(family_name, pname->fgname, family_len);
    family_name[family_len] = '\0';
    return family_name;
}

static int personname_cmp_internal(PersonName *a, PersonName *b) {
    char *a_family, *a_given, *b_family, *b_given;
    int cmp;
    a_family = extract_family_name(a);
    a_given = extract_given_name(a);
    b_family = extract_family_name(b);
    b_given = extract_given_name(b);
    cmp = strcmp(a_family, b_family);
    if (cmp != 0) {
        pfree(a_family);
        pfree(a_given);
        pfree(b_family);
        pfree(b_given);
        return cmp;
    }
    cmp = strcmp(a_given, b_given);
    pfree(a_family);
    pfree(a_given);
    pfree(b_family);
    pfree(b_given);

    return cmp;
}

PG_FUNCTION_INFO_V1(personname_eq);

Datum personname_eq(PG_FUNCTION_ARGS) {
    PersonName *a = (PersonName *) PG_GETARG_POINTER(0);
    PersonName *b = (PersonName *) PG_GETARG_POINTER(1);

    PG_RETURN_BOOL(personname_cmp_internal(a, b) == 0);
}

PG_FUNCTION_INFO_V1(personname_neq);

Datum personname_neq(PG_FUNCTION_ARGS) {
    PersonName *a = (PersonName *) PG_GETARG_POINTER(0);
    PersonName *b = (PersonName *) PG_GETARG_POINTER(1);

    PG_RETURN_BOOL(personname_cmp_internal(a, b) != 0);
}

PG_FUNCTION_INFO_V1(personname_gt);

Datum personname_gt(PG_FUNCTION_ARGS) {
    PersonName *a = (PersonName *) PG_GETARG_POINTER(0);
    PersonName *b = (PersonName *) PG_GETARG_POINTER(1);

    PG_RETURN_BOOL(personname_cmp_internal(a, b) > 0);
}

PG_FUNCTION_INFO_V1(personname_lt);

Datum personname_lt(PG_FUNCTION_ARGS) {
    PersonName *a = (PersonName *) PG_GETARG_POINTER(0);
    PersonName *b = (PersonName *) PG_GETARG_POINTER(1);

    PG_RETURN_BOOL(personname_cmp_internal(a, b) < 0);
}

PG_FUNCTION_INFO_V1(personname_gte);

Datum personname_gte(PG_FUNCTION_ARGS) {
    PersonName *a = (PersonName *) PG_GETARG_POINTER(0);
    PersonName *b = (PersonName *) PG_GETARG_POINTER(1);

    PG_RETURN_BOOL(personname_cmp_internal(a, b) >= 0);
}

PG_FUNCTION_INFO_V1(personname_lte);

Datum personname_lte(PG_FUNCTION_ARGS) {
    PersonName *a = (PersonName *) PG_GETARG_POINTER(0);
    PersonName *b = (PersonName *) PG_GETARG_POINTER(1);

    PG_RETURN_BOOL(personname_cmp_internal(a, b) <= 0);
}

PG_FUNCTION_INFO_V1(personname_cmp);

Datum personname_cmp(PG_FUNCTION_ARGS) {
    PersonName *a = (PersonName *) PG_GETARG_POINTER(0);
    PersonName *b = (PersonName *) PG_GETARG_POINTER(1);

    PG_RETURN_INT32(personname_cmp_internal(a, b));
}

// familyName givenName
PG_FUNCTION_INFO_V1(family);

Datum family(PG_FUNCTION_ARGS) {
    char *comma_position;
    int family_len;
    char *family_name;
    text *result_text;
    PersonName *pname = (PersonName *) PG_GETARG_POINTER(0);
    comma_position = strchr(pname->fgname, ',');
    if (comma_position == NULL) {
        ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("Invalid person name format")));
    }
    family_len = comma_position - pname->fgname;
    family_name = (char *) palloc(family_len + 1);
    memcpy(family_name, pname->fgname, family_len);
    family_name[family_len] = '\0';
    result_text = cstring_to_text(family_name);
    PG_RETURN_TEXT_P(result_text);
}

PG_FUNCTION_INFO_V1(given);

Datum given(PG_FUNCTION_ARGS) {
    char *comma_position;
    int given_len;
    char *given_name;
    text *result_text;
    PersonName *pname = (PersonName *) PG_GETARG_POINTER(0);
    comma_position = strchr(pname->fgname, ',');
    if (comma_position == NULL) {
        ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("Invalid family name format")));
    }
    given_len = strlen(comma_position + 1);
    given_name = (char *) palloc(given_len + 1);
    memcpy(given_name, comma_position + 1, given_len);
    given_name[given_len] = '\0';
    result_text = cstring_to_text(given_name);
    PG_RETURN_TEXT_P(result_text);
}

// showName
PG_FUNCTION_INFO_V1(show);

Datum show(PG_FUNCTION_ARGS) {
	PersonName *pname = (PersonName *)PG_GETARG_POINTER(0);
    char *result;
    char* comma_pos;
    char* show_name;
    char* space_pos;
    int length;
    text *result_text;
    //get the full name string
    int32 input_len = VARSIZE(pname) - VARHDRSZ;
    result = (char *)palloc(input_len + 1);
    memcpy(result, pname->fgname, input_len);
    result[input_len] = '\0';
	//split familyname and givenname
    comma_pos = strchr(result, ',');
    if (comma_pos == NULL) {
        ereport(ERROR, (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("Invalid show name format")));
    }
	//locate the first space
    space_pos = comma_pos + 1;
    while (*space_pos != ' ' && *space_pos != '\0') {
        space_pos++;
    }

    length = space_pos - comma_pos - 1; 
    show_name = (char*)palloc(length + comma_pos - result + 2);

    memcpy(show_name, comma_pos + 1, length);  
    show_name[length] = ' '; 
    memcpy(show_name + length + 1, result, comma_pos - result); 
    show_name[length + comma_pos - result + 1] = '\0'; 
	result_text = cstring_to_text(show_name);
    PG_RETURN_TEXT_P(result_text);
}

PG_FUNCTION_INFO_V1(personname_hash);

Datum personname_hash(PG_FUNCTION_ARGS) {
    PersonName *name = (PersonName *)PG_GETARG_POINTER(0);
    int32 len = VARSIZE(name) - VARHDRSZ;
    uint32 hash_val = DatumGetUInt32(hash_any((unsigned char *)name->fgname, len));
    PG_RETURN_UINT32(hash_val);
}

