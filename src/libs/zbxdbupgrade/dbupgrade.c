/*
** Zabbix
** Copyright (C) 2001-2022 Zabbix SIA
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/

#include "zbxdbupgrade.h"
#include "dbupgrade.h"
#include "zbxdbschema.h"

#include "zbxdbhigh.h"
#include "log.h"
#include "zbxregexp.h"
#include "zbxeval.h"

typedef struct
{
	zbx_dbpatch_t	*patches;
	const char	*description;
}
zbx_db_version_t;

#ifdef HAVE_MYSQL
#	define ZBX_DB_TABLE_OPTIONS	" engine=innodb"
#	define ZBX_DROP_FK		" drop foreign key"
#else
#	define ZBX_DB_TABLE_OPTIONS	""
#	define ZBX_DROP_FK		" drop constraint"
#endif

#if defined(HAVE_POSTGRESQL)
#	define ZBX_DB_ALTER_COLUMN	" alter"
#else
#	define ZBX_DB_ALTER_COLUMN	" modify"
#endif

#if defined(HAVE_POSTGRESQL)
#	define ZBX_DB_SET_TYPE		" type"
#else
#	define ZBX_DB_SET_TYPE		""
#endif

/* NOTE: Do not forget to sync changes in ZBX_TYPE_*_STR defines for Oracle with zbx_oracle_column_type()! */

#if defined(HAVE_MYSQL)
#	define ZBX_TYPE_ID_STR			"bigint unsigned"
#	define ZBX_TYPE_FLOAT_STR		"double precision"
#	define ZBX_TYPE_UINT_STR		"bigint unsigned"
#	define ZBX_TYPE_LONGTEXT_STR		"longtext"
#	define ZBX_TYPE_SERIAL_STR		"bigint unsigned"
#	define ZBX_TYPE_SERIAL_SUFFIX_STR	"auto_increment"
#elif defined(HAVE_ORACLE)
#	define ZBX_TYPE_ID_STR			"number(20)"
#	define ZBX_TYPE_FLOAT_STR		"binary_double"
#	define ZBX_TYPE_UINT_STR		"number(20)"
#	define ZBX_TYPE_LONGTEXT_STR		"nclob"
#	define ZBX_TYPE_SERIAL_STR		"number(20)"
#	define ZBX_TYPE_SERIAL_SUFFIX_STR	""
#elif defined(HAVE_POSTGRESQL)
#	define ZBX_TYPE_ID_STR			"bigint"
#	define ZBX_TYPE_FLOAT_STR		"double precision"
#	define ZBX_TYPE_UINT_STR		"numeric(20)"
#	define ZBX_TYPE_LONGTEXT_STR		"text"
#	define ZBX_TYPE_SERIAL_STR		"bigserial"
#	define ZBX_TYPE_SERIAL_SUFFIX_STR	""
#endif

#if defined(HAVE_ORACLE)
#	define ZBX_TYPE_INT_STR		"number(10)"
#	define ZBX_TYPE_CHAR_STR	"nvarchar2"
#	define ZBX_TYPE_SHORTTEXT_STR	"nvarchar2(2048)"
#	define ZBX_TYPE_TEXT_STR	"nclob"
#else
#	define ZBX_TYPE_INT_STR		"integer"
#	define ZBX_TYPE_CHAR_STR	"varchar"
#	define ZBX_TYPE_SHORTTEXT_STR	"text"
#	define ZBX_TYPE_TEXT_STR	"text"
#endif

#define ZBX_FIRST_DB_VERSION		2010000

#ifndef HAVE_SQLITE3
static void	DBfield_type_string(char **sql, size_t *sql_alloc, size_t *sql_offset, const ZBX_FIELD *field)
{
	switch (field->type)
	{
		case ZBX_TYPE_ID:
			zbx_strcpy_alloc(sql, sql_alloc, sql_offset, ZBX_TYPE_ID_STR);
			break;
		case ZBX_TYPE_INT:
			zbx_strcpy_alloc(sql, sql_alloc, sql_offset, ZBX_TYPE_INT_STR);
			break;
		case ZBX_TYPE_CHAR:
			zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s(%hu)", ZBX_TYPE_CHAR_STR, field->length);
			break;
		case ZBX_TYPE_FLOAT:
			zbx_strcpy_alloc(sql, sql_alloc, sql_offset, ZBX_TYPE_FLOAT_STR);
			break;
		case ZBX_TYPE_UINT:
			zbx_strcpy_alloc(sql, sql_alloc, sql_offset, ZBX_TYPE_UINT_STR);
			break;
		case ZBX_TYPE_LONGTEXT:
			zbx_strcpy_alloc(sql, sql_alloc, sql_offset, ZBX_TYPE_LONGTEXT_STR);
			break;
		case ZBX_TYPE_SHORTTEXT:
			zbx_strcpy_alloc(sql, sql_alloc, sql_offset, ZBX_TYPE_SHORTTEXT_STR);
			break;
		case ZBX_TYPE_TEXT:
			zbx_strcpy_alloc(sql, sql_alloc, sql_offset, ZBX_TYPE_TEXT_STR);
			break;
		case ZBX_TYPE_CUID:
			zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s(%d)", ZBX_TYPE_CHAR_STR, CUID_LEN - 1);
			break;
		case ZBX_TYPE_SERIAL:
			zbx_strcpy_alloc(sql, sql_alloc, sql_offset, ZBX_TYPE_SERIAL_STR);
			break;
		default:
			assert(0);
	}
}

static void	DBfield_type_suffix_string(char **sql, size_t *sql_alloc, size_t *sql_offset, const ZBX_FIELD *field)
{
	switch (field->type)
	{
		case ZBX_TYPE_ID:
		case ZBX_TYPE_INT:
		case ZBX_TYPE_CHAR:
		case ZBX_TYPE_FLOAT:
		case ZBX_TYPE_UINT:
		case ZBX_TYPE_LONGTEXT:
		case ZBX_TYPE_SHORTTEXT:
		case ZBX_TYPE_TEXT:
		case ZBX_TYPE_CUID:
			return;
		case ZBX_TYPE_SERIAL:
			zbx_snprintf_alloc(sql, sql_alloc, sql_offset, " %s", ZBX_TYPE_SERIAL_SUFFIX_STR);
			break;
		default:
			assert(0);
	}
}

#ifdef HAVE_ORACLE
typedef enum
{
	ZBX_ORACLE_COLUMN_TYPE_NUMERIC,
	ZBX_ORACLE_COLUMN_TYPE_CHARACTER,
	ZBX_ORACLE_COLUMN_TYPE_DOUBLE,
	ZBX_ORACLE_COLUMN_TYPE_UNKNOWN
}
zbx_oracle_column_type_t;

/******************************************************************************
 *                                                                            *
 * Purpose: determine whether column type is character or numeric             *
 *                                                                            *
 * Parameters: field_type - [IN] column type in Zabbix definitions            *
 *                                                                            *
 * Return value: column type (character/raw, numeric) in Oracle definitions   *
 *                                                                            *
 * Comments: The size of a character or raw column or the precision of a      *
 *           numeric column can be changed, whether or not all the rows       *
 *           contain nulls. Otherwise in order to change the datatype of a    *
 *           column all rows of the column must contain nulls.                *
 *                                                                            *
 ******************************************************************************/
static zbx_oracle_column_type_t	zbx_oracle_column_type(unsigned char field_type)
{
	switch (field_type)
	{
		case ZBX_TYPE_ID:
		case ZBX_TYPE_INT:
		case ZBX_TYPE_UINT:
			return ZBX_ORACLE_COLUMN_TYPE_NUMERIC;
		case ZBX_TYPE_CHAR:
		case ZBX_TYPE_LONGTEXT:
		case ZBX_TYPE_SHORTTEXT:
		case ZBX_TYPE_TEXT:
			return ZBX_ORACLE_COLUMN_TYPE_CHARACTER;
		case ZBX_TYPE_FLOAT:
			return ZBX_ORACLE_COLUMN_TYPE_DOUBLE;
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			return ZBX_ORACLE_COLUMN_TYPE_UNKNOWN;
	}
}
#endif

static void	DBfield_definition_string(char **sql, size_t *sql_alloc, size_t *sql_offset, const ZBX_FIELD *field)
{
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, ZBX_FS_SQL_NAME " ", field->name);
	DBfield_type_string(sql, sql_alloc, sql_offset, field);
	if (NULL != field->default_value)
	{
		char	*default_value_esc;

#if defined(HAVE_MYSQL)
		switch (field->type)
		{
			case ZBX_TYPE_BLOB:
			case ZBX_TYPE_TEXT:
			case ZBX_TYPE_SHORTTEXT:
			case ZBX_TYPE_LONGTEXT:
				/* MySQL: BLOB and TEXT columns cannot be assigned a default value */
				break;
			default:
#endif
				default_value_esc = DBdyn_escape_string(field->default_value);
				zbx_snprintf_alloc(sql, sql_alloc, sql_offset, " default '%s'", default_value_esc);
				zbx_free(default_value_esc);
#if defined(HAVE_MYSQL)
		}
#endif
	}

	if (0 != (field->flags & ZBX_NOTNULL))
	{
#if defined(HAVE_ORACLE)
		switch (field->type)
		{
			case ZBX_TYPE_INT:
			case ZBX_TYPE_FLOAT:
			case ZBX_TYPE_BLOB:
			case ZBX_TYPE_UINT:
			case ZBX_TYPE_ID:
				zbx_strcpy_alloc(sql, sql_alloc, sql_offset, " not null");
				break;
			default:	/* ZBX_TYPE_CHAR, ZBX_TYPE_TEXT, ZBX_TYPE_SHORTTEXT or ZBX_TYPE_LONGTEXT */
				/* nothing to do */;
		}
#else
		zbx_strcpy_alloc(sql, sql_alloc, sql_offset, " not null");
#endif
	}

	DBfield_type_suffix_string(sql, sql_alloc, sql_offset, field);
}

static void	DBcreate_table_sql(char **sql, size_t *sql_alloc, size_t *sql_offset, const ZBX_TABLE *table)
{
	int	i;

	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "create table %s (\n", table->table);

	for (i = 0; NULL != table->fields[i].name; i++)
	{
		if (0 != i)
			zbx_strcpy_alloc(sql, sql_alloc, sql_offset, ",\n");
		DBfield_definition_string(sql, sql_alloc, sql_offset, &table->fields[i]);
	}
	if ('\0' != *table->recid)
		zbx_snprintf_alloc(sql, sql_alloc, sql_offset, ",\nprimary key (%s)", table->recid);

	zbx_strcpy_alloc(sql, sql_alloc, sql_offset, "\n)" ZBX_DB_TABLE_OPTIONS);
}

static void	DBrename_table_sql(char **sql, size_t *sql_alloc, size_t *sql_offset, const char *table_name,
		const char *new_name)
{
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "alter table " ZBX_FS_SQL_NAME " rename to " ZBX_FS_SQL_NAME,
			table_name, new_name);
}

static void	DBdrop_table_sql(char **sql, size_t *sql_alloc, size_t *sql_offset, const char *table_name)
{
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "drop table %s", table_name);
}

static void	DBset_default_sql(char **sql, size_t *sql_alloc, size_t *sql_offset,
		const char *table_name, const ZBX_FIELD *field)
{
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "alter table %s" ZBX_DB_ALTER_COLUMN " ", table_name);

#if defined(HAVE_MYSQL)
	DBfield_definition_string(sql, sql_alloc, sql_offset, field);
#elif defined(HAVE_ORACLE)
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s default '%s'", field->name, field->default_value);
#else
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s set default '%s'", field->name, field->default_value);
#endif
}

static void	DBdrop_default_sql(char **sql, size_t *sql_alloc, size_t *sql_offset,
		const char *table_name, const ZBX_FIELD *field)
{
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "alter table %s" ZBX_DB_ALTER_COLUMN " ", table_name);

#if defined(HAVE_MYSQL)
	DBfield_definition_string(sql, sql_alloc, sql_offset, field);
#elif defined(HAVE_ORACLE)
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s default null", field->name);
#else
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s drop default", field->name);
#endif
}

static void	DBmodify_field_type_sql(char **sql, size_t *sql_alloc, size_t *sql_offset,
		const char *table_name, const ZBX_FIELD *field)
{
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "alter table " ZBX_FS_SQL_NAME ZBX_DB_ALTER_COLUMN " ",
			table_name);

#ifdef HAVE_MYSQL
	DBfield_definition_string(sql, sql_alloc, sql_offset, field);
#else
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s" ZBX_DB_SET_TYPE " ", field->name);
	DBfield_type_string(sql, sql_alloc, sql_offset, field);
#ifdef HAVE_POSTGRESQL
	if (NULL != field->default_value)
	{
		zbx_strcpy_alloc(sql, sql_alloc, sql_offset, ";\n");
		DBset_default_sql(sql, sql_alloc, sql_offset, table_name, field);
	}
#endif
#endif
}

static void	DBdrop_not_null_sql(char **sql, size_t *sql_alloc, size_t *sql_offset,
		const char *table_name, const ZBX_FIELD *field)
{
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "alter table %s" ZBX_DB_ALTER_COLUMN " ", table_name);

#if defined(HAVE_MYSQL)
	DBfield_definition_string(sql, sql_alloc, sql_offset, field);
#elif defined(HAVE_ORACLE)
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s null", field->name);
#else
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s drop not null", field->name);
#endif
}

static void	DBset_not_null_sql(char **sql, size_t *sql_alloc, size_t *sql_offset,
		const char *table_name, const ZBX_FIELD *field)
{
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "alter table %s" ZBX_DB_ALTER_COLUMN " ", table_name);

#if defined(HAVE_MYSQL)
	DBfield_definition_string(sql, sql_alloc, sql_offset, field);
#elif defined(HAVE_ORACLE)
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s not null", field->name);
#else
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "%s set not null", field->name);
#endif
}

static void	DBadd_field_sql(char **sql, size_t *sql_alloc, size_t *sql_offset,
		const char *table_name, const ZBX_FIELD *field)
{
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "alter table " ZBX_FS_SQL_NAME " add ", table_name);
	DBfield_definition_string(sql, sql_alloc, sql_offset, field);
}

static void	DBrename_field_sql(char **sql, size_t *sql_alloc, size_t *sql_offset,
		const char *table_name, const char *field_name, const ZBX_FIELD *field)
{
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "alter table " ZBX_FS_SQL_NAME " ", table_name);

#ifdef HAVE_MYSQL
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "change column " ZBX_FS_SQL_NAME " ", field_name);
	DBfield_definition_string(sql, sql_alloc, sql_offset, field);
#else
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "rename column " ZBX_FS_SQL_NAME " to " ZBX_FS_SQL_NAME,
			field_name, field->name);
#endif
}

static void	DBdrop_field_sql(char **sql, size_t *sql_alloc, size_t *sql_offset,
		const char *table_name, const char *field_name)
{
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "alter table %s drop column %s", table_name, field_name);
}

static void	DBcreate_index_sql(char **sql, size_t *sql_alloc, size_t *sql_offset,
		const char *table_name, const char *index_name, const char *fields, int unique)
{
	zbx_strcpy_alloc(sql, sql_alloc, sql_offset, "create");
	if (0 != unique)
		zbx_strcpy_alloc(sql, sql_alloc, sql_offset, " unique");
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, " index %s on %s (%s)", index_name, table_name, fields);
}

static void	DBdrop_index_sql(char **sql, size_t *sql_alloc, size_t *sql_offset,
		const char *table_name, const char *index_name)
{
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "drop index %s", index_name);
#ifdef HAVE_MYSQL
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, " on %s", table_name);
#else
	ZBX_UNUSED(table_name);
#endif
}

static void	DBrename_index_sql(char **sql, size_t *sql_alloc, size_t *sql_offset, const char *table_name,
		const char *old_name, const char *new_name, const char *fields, int unique)
{
#if defined(HAVE_MYSQL)
	DBcreate_index_sql(sql, sql_alloc, sql_offset, table_name, new_name, fields, unique);
	zbx_strcpy_alloc(sql, sql_alloc, sql_offset, ";\n");
	DBdrop_index_sql(sql, sql_alloc, sql_offset, table_name, old_name);
	zbx_strcpy_alloc(sql, sql_alloc, sql_offset, ";\n");
#elif defined(HAVE_ORACLE) || defined(HAVE_POSTGRESQL)
	ZBX_UNUSED(table_name);
	ZBX_UNUSED(fields);
	ZBX_UNUSED(unique);
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "alter index %s rename to %s", old_name, new_name);
#endif
}

static void	DBadd_foreign_key_sql(char **sql, size_t *sql_alloc, size_t *sql_offset,
		const char *table_name, int id, const ZBX_FIELD *field)
{
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset,
			"alter table " ZBX_FS_SQL_NAME " add constraint c_%s_%d foreign key (" ZBX_FS_SQL_NAME ")"
					" references " ZBX_FS_SQL_NAME " (" ZBX_FS_SQL_NAME ")", table_name, table_name,
					id, field->name, field->fk_table, field->fk_field);
	if (0 != (field->fk_flags & ZBX_FK_CASCADE_DELETE))
		zbx_strcpy_alloc(sql, sql_alloc, sql_offset, " on delete cascade");
}

static void	DBdrop_foreign_key_sql(char **sql, size_t *sql_alloc, size_t *sql_offset,
		const char *table_name, int id)
{
	zbx_snprintf_alloc(sql, sql_alloc, sql_offset, "alter table %s" ZBX_DROP_FK " c_%s_%d",
			table_name, table_name, id);
}

int	DBcreate_table(const ZBX_TABLE *table)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	ret = FAIL;

	DBcreate_table_sql(&sql, &sql_alloc, &sql_offset, table);

	if (ZBX_DB_OK <= DBexecute("%s", sql))
		ret = SUCCEED;

	zbx_free(sql);

	return ret;
}

int	DBrename_table(const char *table_name, const char *new_name)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	ret = FAIL;

	DBrename_table_sql(&sql, &sql_alloc, &sql_offset, table_name, new_name);

	if (ZBX_DB_OK <= DBexecute("%s", sql))
		ret = SUCCEED;

	zbx_free(sql);

	return ret;
}

int	DBdrop_table(const char *table_name)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	ret = FAIL;

	DBdrop_table_sql(&sql, &sql_alloc, &sql_offset, table_name);

	if (ZBX_DB_OK <= DBexecute("%s", sql))
		ret = SUCCEED;

	zbx_free(sql);

	return ret;
}

int	DBadd_field(const char *table_name, const ZBX_FIELD *field)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	ret = FAIL;

	DBadd_field_sql(&sql, &sql_alloc, &sql_offset, table_name, field);

	if (ZBX_DB_OK <= DBexecute("%s", sql))
		ret = SUCCEED;

	zbx_free(sql);

	return ret;
}

int	DBrename_field(const char *table_name, const char *field_name, const ZBX_FIELD *field)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	ret = FAIL;

	DBrename_field_sql(&sql, &sql_alloc, &sql_offset, table_name, field_name, field);

	if (ZBX_DB_OK <= DBexecute("%s", sql))
		ret = SUCCEED;

	zbx_free(sql);

	return ret;
}

#ifdef HAVE_ORACLE
static int	DBmodify_field_type_with_copy(const char *table_name, const ZBX_FIELD *field)
{
#define ZBX_OLD_FIELD	"zbx_old_tmp"

	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	ret = FAIL;

	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "alter table %s rename column %s to " ZBX_OLD_FIELD,
			table_name, field->name);

	if (ZBX_DB_OK > DBexecute("%s", sql))
		goto out;

	if (ZBX_DB_OK > DBadd_field(table_name, field))
		goto out;

	sql_offset = 0;
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update %s set %s=" ZBX_OLD_FIELD, table_name,
			field->name);

	if (ZBX_DB_OK > DBexecute("%s", sql))
		goto out;

	ret = DBdrop_field(table_name, ZBX_OLD_FIELD);
out:
	zbx_free(sql);

	return ret;

#undef ZBX_OLD_FIELD
}

#define ZBX_FS_DB_SEQUENCE	"%s_seq"
#define ZBX_FS_DB_TRIGGER	"%s_tr"

int	DBcreate_serial_sequence(const char *table_name)
{
	if (ZBX_DB_OK > DBexecute("create sequence " ZBX_FS_DB_SEQUENCE " start with 1 increment by 1 nomaxvalue",
			table_name))
	{
		return FAIL;
	}

	return SUCCEED;
}

int	DBcreate_serial_trigger(const char *table_name, const char *field_name)
{
	if (ZBX_DB_OK > DBexecute("create trigger " ZBX_FS_DB_TRIGGER " before insert on %s for each row\n"
					"begin\n"
						"select " ZBX_FS_DB_SEQUENCE ".nextval into :new.%s from dual;\n"
					"end;",
				table_name, table_name, table_name, field_name))
	{
		return FAIL;
	}

	return SUCCEED;
}

#endif

int	DBmodify_field_type(const char *table_name, const ZBX_FIELD *field, const ZBX_FIELD *old_field)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	ret = FAIL;

#ifndef HAVE_ORACLE
	ZBX_UNUSED(old_field);
#else
	/* Oracle cannot change column type in a general case if column contents are not null. Conversions like   */
	/* number -> nvarchar2 or nvarchar2 -> nclob need special processing. New column is created with desired  */
	/* datatype and data from old column is copied there. Then old column is dropped. This method does not    */
	/* preserve column order.                                                                                 */
	/* NOTE: Existing column indexes and constraints are not respected by the current implementation!         */

	if (NULL != old_field && (zbx_oracle_column_type(old_field->type) != zbx_oracle_column_type(field->type) ||
			ZBX_ORACLE_COLUMN_TYPE_DOUBLE == zbx_oracle_column_type(field->type) ||
			((ZBX_TYPE_TEXT == field->type || ZBX_TYPE_LONGTEXT == field->type) &&
				(ZBX_TYPE_SHORTTEXT == old_field->type || ZBX_TYPE_CHAR == old_field->type))))
	{
		return DBmodify_field_type_with_copy(table_name, field);
	}
#endif
	DBmodify_field_type_sql(&sql, &sql_alloc, &sql_offset, table_name, field);

	if (ZBX_DB_OK <= DBexecute("%s", sql))
		ret = SUCCEED;

	zbx_free(sql);

	return ret;
}

int	DBset_not_null(const char *table_name, const ZBX_FIELD *field)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	ret = FAIL;

	DBset_not_null_sql(&sql, &sql_alloc, &sql_offset, table_name, field);

	if (ZBX_DB_OK <= DBexecute("%s", sql))
		ret = SUCCEED;

	zbx_free(sql);

	return ret;
}

int	DBset_default(const char *table_name, const ZBX_FIELD *field)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	ret = FAIL;

	DBset_default_sql(&sql, &sql_alloc, &sql_offset, table_name, field);

	if (ZBX_DB_OK <= DBexecute("%s", sql))
		ret = SUCCEED;

	zbx_free(sql);

	return ret;
}

int	DBdrop_default(const char *table_name, const ZBX_FIELD *field)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	ret = FAIL;

	DBdrop_default_sql(&sql, &sql_alloc, &sql_offset, table_name, field);

	if (ZBX_DB_OK <= DBexecute("%s", sql))
		ret = SUCCEED;

	zbx_free(sql);

	return ret;
}

int	DBdrop_not_null(const char *table_name, const ZBX_FIELD *field)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	ret = FAIL;

	DBdrop_not_null_sql(&sql, &sql_alloc, &sql_offset, table_name, field);

	if (ZBX_DB_OK <= DBexecute("%s", sql))
		ret = SUCCEED;

	zbx_free(sql);

	return ret;
}

int	DBdrop_field(const char *table_name, const char *field_name)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	ret = FAIL;

	DBdrop_field_sql(&sql, &sql_alloc, &sql_offset, table_name, field_name);

	if (ZBX_DB_OK <= DBexecute("%s", sql))
		ret = SUCCEED;

	zbx_free(sql);

	return ret;
}

int	DBcreate_index(const char *table_name, const char *index_name, const char *fields, int unique)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	ret = FAIL;

	DBcreate_index_sql(&sql, &sql_alloc, &sql_offset, table_name, index_name, fields, unique);

	if (ZBX_DB_OK <= DBexecute("%s", sql))
		ret = SUCCEED;

	zbx_free(sql);

	return ret;
}

int	DBdrop_index(const char *table_name, const char *index_name)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	ret = FAIL;

	DBdrop_index_sql(&sql, &sql_alloc, &sql_offset, table_name, index_name);

	if (ZBX_DB_OK <= DBexecute("%s", sql))
		ret = SUCCEED;

	zbx_free(sql);

	return ret;
}

int	DBrename_index(const char *table_name, const char *old_name, const char *new_name, const char *fields,
				int unique)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	ret = FAIL;

	DBrename_index_sql(&sql, &sql_alloc, &sql_offset, table_name, old_name, new_name, fields, unique);

	if (ZBX_DB_OK <= DBexecute("%s", sql))
		ret = SUCCEED;

	zbx_free(sql);

	return ret;
}

int	DBadd_foreign_key(const char *table_name, int id, const ZBX_FIELD *field)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	ret = FAIL;

	DBadd_foreign_key_sql(&sql, &sql_alloc, &sql_offset, table_name, id, field);

	if (ZBX_DB_OK <= DBexecute("%s", sql))
		ret = SUCCEED;

	zbx_free(sql);

	return ret;
}

int	DBdrop_foreign_key(const char *table_name, int id)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	ret = FAIL;

	DBdrop_foreign_key_sql(&sql, &sql_alloc, &sql_offset, table_name, id);

	if (ZBX_DB_OK <= DBexecute("%s", sql))
		ret = SUCCEED;

	zbx_free(sql);

	return ret;
}

static int	DBcreate_dbversion_table(void)
{
	const ZBX_TABLE	table =
			{"dbversion", "", 0,
				{
					{"mandatory", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
					{"optional", "0", NULL, NULL, 0, ZBX_TYPE_INT, ZBX_NOTNULL, 0},
					{NULL}
				},
				NULL
			};
	int		ret;

	DBbegin();
	if (SUCCEED == (ret = DBcreate_table(&table)))
	{
		if (ZBX_DB_OK > DBexecute("insert into dbversion (mandatory,optional) values (%d,%d)",
				ZBX_FIRST_DB_VERSION, ZBX_FIRST_DB_VERSION))
		{
			ret = FAIL;
		}
	}

	return DBend(ret);
}

static int	DBset_version(int version, unsigned char mandatory)
{
	char	sql[64];
	size_t	offset;

	offset = zbx_snprintf(sql, sizeof(sql),  "update dbversion set ");
	if (0 != mandatory)
		offset += zbx_snprintf(sql + offset, sizeof(sql) - offset, "mandatory=%d,", version);
	zbx_snprintf(sql + offset, sizeof(sql) - offset, "optional=%d", version);

	if (ZBX_DB_OK <= DBexecute("%s", sql))
		return SUCCEED;

	return FAIL;
}

#endif	/* not HAVE_SQLITE3 */

zbx_get_program_type_f	DBget_program_type_cb;

extern zbx_dbpatch_t	DBPATCH_VERSION(2010)[];
extern zbx_dbpatch_t	DBPATCH_VERSION(2020)[];
extern zbx_dbpatch_t	DBPATCH_VERSION(2030)[];
extern zbx_dbpatch_t	DBPATCH_VERSION(2040)[];
extern zbx_dbpatch_t	DBPATCH_VERSION(2050)[];
extern zbx_dbpatch_t	DBPATCH_VERSION(3000)[];
extern zbx_dbpatch_t	DBPATCH_VERSION(3010)[];
extern zbx_dbpatch_t	DBPATCH_VERSION(3020)[];
extern zbx_dbpatch_t	DBPATCH_VERSION(3030)[];
extern zbx_dbpatch_t	DBPATCH_VERSION(3040)[];
extern zbx_dbpatch_t	DBPATCH_VERSION(3050)[];
extern zbx_dbpatch_t	DBPATCH_VERSION(4000)[];
extern zbx_dbpatch_t	DBPATCH_VERSION(4010)[];
extern zbx_dbpatch_t	DBPATCH_VERSION(4020)[];
extern zbx_dbpatch_t	DBPATCH_VERSION(4030)[];
extern zbx_dbpatch_t	DBPATCH_VERSION(4040)[];
extern zbx_dbpatch_t	DBPATCH_VERSION(4050)[];
extern zbx_dbpatch_t	DBPATCH_VERSION(5000)[];
extern zbx_dbpatch_t	DBPATCH_VERSION(5010)[];
extern zbx_dbpatch_t	DBPATCH_VERSION(5020)[];
extern zbx_dbpatch_t	DBPATCH_VERSION(5030)[];
extern zbx_dbpatch_t	DBPATCH_VERSION(5040)[];
extern zbx_dbpatch_t	DBPATCH_VERSION(5050)[];
extern zbx_dbpatch_t	DBPATCH_VERSION(6000)[];
extern zbx_dbpatch_t	DBPATCH_VERSION(6010)[];
extern zbx_dbpatch_t	DBPATCH_VERSION(6020)[];
extern zbx_dbpatch_t	DBPATCH_VERSION(6030)[];

static zbx_db_version_t dbversions[] = {
	{DBPATCH_VERSION(2010), "2.2 development"},
	{DBPATCH_VERSION(2020), "2.2 maintenance"},
	{DBPATCH_VERSION(2030), "2.4 development"},
	{DBPATCH_VERSION(2040), "2.4 maintenance"},
	{DBPATCH_VERSION(2050), "3.0 development"},
	{DBPATCH_VERSION(3000), "3.0 maintenance"},
	{DBPATCH_VERSION(3010), "3.2 development"},
	{DBPATCH_VERSION(3020), "3.2 maintenance"},
	{DBPATCH_VERSION(3030), "3.4 development"},
	{DBPATCH_VERSION(3040), "3.4 maintenance"},
	{DBPATCH_VERSION(3050), "4.0 development"},
	{DBPATCH_VERSION(4000), "4.0 maintenance"},
	{DBPATCH_VERSION(4010), "4.2 development"},
	{DBPATCH_VERSION(4020), "4.2 maintenance"},
	{DBPATCH_VERSION(4030), "4.4 development"},
	{DBPATCH_VERSION(4040), "4.4 maintenance"},
	{DBPATCH_VERSION(4050), "5.0 development"},
	{DBPATCH_VERSION(5000), "5.0 maintenance"},
	{DBPATCH_VERSION(5010), "5.2 development"},
	{DBPATCH_VERSION(5020), "5.2 maintenance"},
	{DBPATCH_VERSION(5030), "5.4 development"},
	{DBPATCH_VERSION(5040), "5.4 maintenance"},
	{DBPATCH_VERSION(5050), "6.0 development"},
	{DBPATCH_VERSION(6000), "6.0 maintenance"},
	{DBPATCH_VERSION(6010), "6.2 development"},
	{DBPATCH_VERSION(6020), "6.2 maintenance"},
	{DBPATCH_VERSION(6030), "6.4 development"},
	{NULL}
};

static void	DBget_version(int *mandatory, int *optional)
{
	DB_RESULT	result;
	DB_ROW		row;

	*mandatory = -1;
	*optional = -1;

	result = DBselect("select mandatory,optional from dbversion");

	if (NULL != (row = DBfetch(result)))
	{
		*mandatory = atoi(row[0]);
		*optional = atoi(row[1]);
	}
	DBfree_result(result);

	if (-1 == *mandatory)
	{
		zabbix_log(LOG_LEVEL_CRIT, "Cannot get the database version. Exiting ...");
		exit(EXIT_FAILURE);
	}
}

unsigned char	DBget_program_type(void)
{
	return DBget_program_type_cb();
}

void	zbx_init_library_dbupgrade(zbx_get_program_type_f get_program_type_cb)
{
	DBget_program_type_cb = get_program_type_cb;
}

int	DBcheck_version(void)
{
	const char		*dbversion_table_name = "dbversion";
	int			db_mandatory, db_optional, required, ret = FAIL, i;
	zbx_db_version_t	*dbversion;
	zbx_dbpatch_t		*patches;

#ifndef HAVE_SQLITE3
	int			total = 0, current = 0, completed, last_completed = -1, optional_num = 0;
#endif
	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	required = ZBX_FIRST_DB_VERSION;

	/* find out the required version number by getting the last mandatory version */
	/* of the last version patch array                                            */
	for (dbversion = dbversions; NULL != dbversion->patches; dbversion++)
		;

	patches = (--dbversion)->patches;

	for (i = 0; 0 != patches[i].version; i++)
	{
		if (0 != patches[i].mandatory)
			required = patches[i].version;
	}

	DBconnect(ZBX_DB_CONNECT_NORMAL);

	if (SUCCEED != DBtable_exists(dbversion_table_name))
	{
#ifndef HAVE_SQLITE3
		zabbix_log(LOG_LEVEL_DEBUG, "%s() \"%s\" does not exist", __func__, dbversion_table_name);

		if (SUCCEED != DBfield_exists("config", "server_check_interval"))
		{
			zabbix_log(LOG_LEVEL_CRIT, "Cannot upgrade database: the database must"
					" correspond to version 2.0 or later. Exiting ...");
			goto out;
		}

		if (SUCCEED != DBcreate_dbversion_table())
			goto out;
#else
		zabbix_log(LOG_LEVEL_CRIT, "The %s does not match Zabbix database."
				" Current database version (mandatory/optional): UNKNOWN."
				" Required mandatory version: %08d.",
				get_program_type_string(DBget_program_type_cb()), required);
		zabbix_log(LOG_LEVEL_CRIT, "Zabbix does not support SQLite3 database upgrade.");

		ret = NOTSUPPORTED;
		goto out;
#endif
	}

	DBget_version(&db_mandatory, &db_optional);

#ifndef HAVE_SQLITE3
	for (dbversion = dbversions; NULL != (patches = dbversion->patches); dbversion++)
	{
		for (i = 0; 0 != patches[i].version; i++)
		{
			if (0 != patches[i].mandatory)
				optional_num = 0;
			else
				optional_num++;

			if (db_optional < patches[i].version)
				total++;
		}
	}

	if (required < db_mandatory)
#else
	if (required != db_mandatory)
#endif
	{
		zabbix_log(LOG_LEVEL_CRIT, "The %s does not match Zabbix database."
				" Current database version (mandatory/optional): %08d/%08d."
				" Required mandatory version: %08d.",
				get_program_type_string(DBget_program_type_cb()), db_mandatory, db_optional,
				required);
#ifdef HAVE_SQLITE3
		if (required > db_mandatory)
			zabbix_log(LOG_LEVEL_WARNING, "Zabbix does not support SQLite3 database upgrade.");
		else
			ret = NOTSUPPORTED;
#endif
		goto out;
	}

	zabbix_log(LOG_LEVEL_INFORMATION, "current database version (mandatory/optional): %08d/%08d",
			db_mandatory, db_optional);
	zabbix_log(LOG_LEVEL_INFORMATION, "required mandatory version: %08d", required);

	ret = SUCCEED;

#ifndef HAVE_SQLITE3
	if (0 == total)
		goto out;

	if (0 != optional_num)
		zabbix_log(LOG_LEVEL_INFORMATION, "optional patches were found");

	zabbix_log(LOG_LEVEL_WARNING, "starting automatic database upgrade");

	for (dbversion = dbversions; NULL != dbversion->patches; dbversion++)
	{
		patches = dbversion->patches;

		for (i = 0; 0 != patches[i].version; i++)
		{
			static sigset_t	orig_mask, mask;

			if (db_optional >= patches[i].version)
				continue;

			/* block signals to prevent interruption of statements that cause an implicit commit */
			sigemptyset(&mask);
			sigaddset(&mask, SIGTERM);
			sigaddset(&mask, SIGINT);
			sigaddset(&mask, SIGQUIT);

			if (0 > sigprocmask(SIG_BLOCK, &mask, &orig_mask))
				zabbix_log(LOG_LEVEL_WARNING, "cannot set sigprocmask to block the user signal");

			DBbegin();

			/* skipping the duplicated patches */
			if ((0 != patches[i].duplicates && patches[i].duplicates <= db_optional) ||
					SUCCEED == (ret = patches[i].function()))
			{
				ret = DBset_version(patches[i].version, patches[i].mandatory);
			}

			ret = DBend(ret);

			if (0 > sigprocmask(SIG_SETMASK, &orig_mask, NULL))
				zabbix_log(LOG_LEVEL_WARNING,"cannot restore sigprocmask");

			if (SUCCEED != ret)
				break;

			current++;
			completed = (int)(100.0 * current / total);

			if (last_completed != completed)
			{
				zabbix_log(LOG_LEVEL_WARNING, "completed %d%% of database upgrade", completed);
				last_completed = completed;
			}
		}

		if (SUCCEED != ret)
			break;
	}

	if (SUCCEED == ret)
	{
		/* clear changelog after successful upgrade, doesn't matter if it fails */
		(void)DBexecute("delete from changelog");

		zabbix_log(LOG_LEVEL_WARNING, "database upgrade fully completed");
	}
	else
		zabbix_log(LOG_LEVEL_CRIT, "database upgrade failed");
#endif	/* not HAVE_SQLITE3 */

out:
	DBclose();

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

int	DBcheck_double_type(void)
{
	DB_RESULT	result;
	DB_ROW		row;
	char		*sql = NULL;
	const int	total_dbl_cols = 4;
	int		ret = FAIL;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	DBconnect(ZBX_DB_CONNECT_NORMAL);

#if defined(HAVE_MYSQL)
	sql = DBdyn_escape_string(CONFIG_DBNAME);
	sql = zbx_dsprintf(sql, "select count(*) from information_schema.columns"
			" where table_schema='%s' and column_type='double'", sql);
#elif defined(HAVE_POSTGRESQL)
	sql = DBdyn_escape_string(NULL == CONFIG_DBSCHEMA || '\0' == *CONFIG_DBSCHEMA ? "public" : CONFIG_DBSCHEMA);
	sql = zbx_dsprintf(sql, "select count(*) from information_schema.columns"
			" where table_schema='%s' and data_type='double precision'", sql);
#elif defined(HAVE_ORACLE)
	sql = zbx_strdup(sql, "select count(*) from user_tab_columns"
			" where data_type='BINARY_DOUBLE'");
#elif defined(HAVE_SQLITE3)
	/* upgrade patch is not required for sqlite3 */
	ret = SUCCEED;
	goto out;
#endif

	if (NULL == (result = DBselect("%s"
			" and ((lower(table_name)='trends'"
					" and (lower(column_name) in ('value_min', 'value_avg', 'value_max')))"
			" or (lower(table_name)='history' and lower(column_name)='value'))", sql)))
	{
		zabbix_log(LOG_LEVEL_WARNING, "cannot select records with columns information");
		goto out;
	}

	if (NULL != (row = DBfetch(result)) && total_dbl_cols == atoi(row[0]))
		ret = SUCCEED;

	DBfree_result(result);
out:
	DBclose();
	zbx_free(sql);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s()", __func__);

	return ret;
}

#ifndef HAVE_SQLITE3

#define ZBX_CHANGELOG_OP_INSERT	1
#define ZBX_CHANGELOG_OP_UPDATE	2
#define ZBX_CHANGELOG_OP_DELETE	3

static int	DBget_changelog_table_by_name(const char *table_name)
{
	const zbx_db_table_changelog_t	*table;

	for (table = changelog_tables; NULL != table->table; table++)
	{
		if (0 == strcmp(table_name, table->table))
			return table->object;
	}

	return FAIL;
}

int	DBcreate_changelog_insert_trigger(const char *table_name, const char *field_name)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	table_type, ret = FAIL;

	if (FAIL == (table_type = DBget_changelog_table_by_name(table_name)))
	{
		THIS_SHOULD_NEVER_HAPPEN;
		return FAIL;
	}

#ifdef HAVE_ORACLE
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"create trigger %s_insert after insert on %s\n"
				"for each row\n"
				"begin\n"
					"insert into changelog (object,objectid,operation,clock)\n"
						"values (%d,:new.%s,%d,(cast(sys_extract_utc(systimestamp) as date)"
						"-date'1970-01-01')*86400);\n"
				"end;", table_name, table_name, table_type, field_name, ZBX_CHANGELOG_OP_INSERT);
#elif HAVE_MYSQL
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"create trigger %s_insert after insert on %s\n"
				"for each row\n"
					"insert into changelog (object,objectid,operation,clock)\n"
						"values (%d,new.%s,%d,unix_timestamp())",
				table_name, table_name, table_type, field_name, ZBX_CHANGELOG_OP_INSERT);
#elif HAVE_POSTGRESQL
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"create or replace function changelog_%s_insert() returns trigger as $$\n"
			"begin\n"
				"insert into changelog (object,objectid,operation,clock)\n"
					"values (%d,new.%s,%d,cast(extract(epoch from now()) as int));\n"
				"return new;\n"
			"end;\n"
			"$$ language plpgsql;\n"
			"create trigger %s_insert after insert on %s\n"
				"for each row\n"
					"execute procedure changelog_%s_insert();",
				table_name, table_type, field_name, ZBX_CHANGELOG_OP_INSERT, table_name, table_name,
				table_name);
#endif

	if (ZBX_DB_OK <= DBexecute("%s", sql))
		ret = SUCCEED;

	zbx_free(sql);

	return ret;
}

int	DBcreate_changelog_update_trigger(const char *table_name, const char *field_name)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	table_type, ret = FAIL;;

	if (FAIL == (table_type = DBget_changelog_table_by_name(table_name)))
	{
		THIS_SHOULD_NEVER_HAPPEN;
		return FAIL;
	}

#ifdef HAVE_ORACLE
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"create trigger %s_update after update on %s\n"
				"for each row\n"
				"begin\n"
					"insert into changelog (object,objectid,operation,clock)\n"
						"values (%d,:old.%s,%d,(cast(sys_extract_utc(systimestamp) as date)"
						"-date'1970-01-01')*86400);\n"
				"end;", table_name, table_name, table_type, field_name, ZBX_CHANGELOG_OP_UPDATE);
#elif HAVE_MYSQL
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"create trigger %s_update after update on %s\n"
				"for each row\n"
					"insert into changelog (object,objectid,operation,clock)\n"
						"values (%d,old.%s,%d,unix_timestamp())",
				table_name, table_name, table_type, field_name, ZBX_CHANGELOG_OP_UPDATE);
#elif HAVE_POSTGRESQL
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"create or replace function changelog_%s_update() returns trigger as $$\n"
			"begin\n"
				"insert into changelog (object,objectid,operation,clock)\n"
					"values (%d,old.%s,%d,cast(extract(epoch from now()) as int));\n"
				"return new;\n"
			"end;\n"
			"$$ language plpgsql;\n"
			"create trigger %s_update after update on %s\n"
				"for each row\n"
					"execute procedure changelog_%s_update();",
				table_name, table_type, field_name, ZBX_CHANGELOG_OP_UPDATE, table_name, table_name,
				table_name);
#endif

	if (ZBX_DB_OK <= DBexecute("%s", sql))
		ret = SUCCEED;

	zbx_free(sql);

	return ret;
}

int	DBcreate_changelog_delete_trigger(const char *table_name, const char *field_name)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	table_type, ret = FAIL;;

	if (FAIL == (table_type = DBget_changelog_table_by_name(table_name)))
	{
		THIS_SHOULD_NEVER_HAPPEN;
		return FAIL;
	}

#ifdef HAVE_ORACLE
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"create trigger %s_delete before delete on %s\n"
				"for each row\n"
				"begin\n"
					"insert into changelog (object,objectid,operation,clock)\n"
						"values (%d,:old.%s,%d,(cast(sys_extract_utc(systimestamp) as date)"
						"-date'1970-01-01')*86400);\n"
				"end;", table_name, table_name, table_type, field_name, ZBX_CHANGELOG_OP_DELETE);
#elif HAVE_MYSQL
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"create trigger %s_delete before delete on %s\n"
				"for each row\n"
					"insert into changelog (object,objectid,operation,clock)\n"
						"values (%d,old.%s,%d,unix_timestamp())",
				table_name, table_name, table_type, field_name, ZBX_CHANGELOG_OP_DELETE);
#elif HAVE_POSTGRESQL
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"create or replace function changelog_%s_delete() returns trigger as $$\n"
			"begin\n"
				"insert into changelog (object,objectid,operation,clock)\n"
					"values (%d,old.%s,%d,cast(extract(epoch from now()) as int));\n"
				"return old;\n"
			"end;\n"
			"$$ language plpgsql;\n"
			"create trigger %s_delete before delete on %s\n"
				"for each row\n"
					"execute procedure changelog_%s_delete();",
				table_name, table_type, field_name, ZBX_CHANGELOG_OP_DELETE, table_name, table_name,
				table_name);
#endif

	if (ZBX_DB_OK <= DBexecute("%s", sql))
		ret = SUCCEED;

	zbx_free(sql);

	return ret;
}

int	zbx_dbupgrade_attach_trigger_with_function_on_insert(const char *table_name,
		const char *original_column_name, const char *indexed_column_name, const char *function,
		const char *idname)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	ret = FAIL;
#ifdef HAVE_ORACLE
	ZBX_UNUSED(idname);

	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"create trigger %s_%s_insert\n"
			"before insert on %s for each row\n"
			"begin\n"
				":new.%s:=%s(:new.%s);\n"
			"end;",
			table_name, indexed_column_name, table_name, indexed_column_name, function,
			original_column_name);
#elif HAVE_MYSQL
	ZBX_UNUSED(idname);

	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"create trigger %s_%s_insert\n"
			"before insert on %s for each row\n"
				"set new.%s=%s(new.%s)",
			table_name, indexed_column_name, table_name, indexed_column_name, function,
			original_column_name);
#elif defined(HAVE_POSTGRESQL)
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"create or replace function %s_%s_%s()\n"
			"returns trigger language plpgsql AS $func$\n"
			"begin\n"
				"update %s set %s=%s(%s)\n"
				"where %s=new.%s;\n"
				"return null;\n"
			"end $func$;\n"

			"create trigger %s_%s_insert after insert\n"
				"on %s\n"
				"for each row execute function %s_%s_%s();",
			table_name, indexed_column_name, function, table_name, indexed_column_name, function,
			original_column_name, idname, idname, table_name, indexed_column_name, table_name,
			table_name, indexed_column_name, function);
#endif
	if (ZBX_DB_OK <= DBexecute("%s", sql))
		ret = SUCCEED;

	zbx_free(sql);

	return ret;
}

int	zbx_dbupgrade_attach_trigger_with_function_on_update(const char *table_name,
		const char *original_column_name, const char *indexed_column_name, const char *function,
		const char *idname)
{
	char	*sql = NULL;
	size_t	sql_alloc = 0, sql_offset = 0;
	int	ret = FAIL;
#ifdef HAVE_ORACLE
	ZBX_UNUSED(idname);

	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"create trigger %s_%s_update\n"
			"before update on %s for each row\n"
			"begin\n"
				"if :new.%s<>:old.%s\n"
				"then\n"
					":new.%s:=%s(:new.%s);\n"
				"end if;\n"
			"end;",
			table_name, indexed_column_name, table_name, original_column_name,
			original_column_name, indexed_column_name, function, original_column_name);
#elif HAVE_MYSQL
	ZBX_UNUSED(idname);

	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"create trigger %s_%s_update\n"
			"before update on %s for each row\n"
			"begin\n"
				"if new.%s<>old.%s\n"
				"then\n"
					"set new.%s=%s(new.%s);\n"
				"end if;\n"
			"end",
			table_name, indexed_column_name, table_name, original_column_name,
			original_column_name, indexed_column_name, function, original_column_name);
#elif defined(HAVE_POSTGRESQL)
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"create or replace function %s_%s_%s()\n"
			"returns trigger language plpgsql AS $func$\n"
			"begin\n"
				"update %s set %s=%s(%s)\n"
				"where %s=new.%s;\n"
				"return null;\n"
			"end $func$;\n"

			"create trigger %s_%s_update after update of %s on %s\n"
				"for each row execute function %s_%s_%s();",
			table_name, indexed_column_name, function, table_name, indexed_column_name, function,
			original_column_name, idname, idname, table_name, indexed_column_name,
			original_column_name, table_name, table_name, indexed_column_name, function);
#endif
	if (ZBX_DB_OK <= DBexecute("%s", sql))
		ret = SUCCEED;

	zbx_free(sql);

	return ret;
}

char	*zbx_update_template_name(char *old)
{
	char	*ptr, new[MAX_STRING_LEN + 1], *ptr_snmp;

#define MIN_TEMPLATE_NAME_LEN	3

	ptr = old;

	if (NULL != zbx_regexp_match(old, "Template (APP|App|DB|Module|Net|OS|SAN|Server|Tel|VM) ", NULL) &&
			1 == sscanf(old, "Template %*[^ ] %" ZBX_STR(MAX_STRING_LEN) "[^\n]s", new) &&
			MIN_TEMPLATE_NAME_LEN <= strlen(new))
	{
		ptr = zbx_strdup(ptr, new);
	}

	ptr_snmp = zbx_string_replace(ptr, "SNMPv2", "SNMP");
	zbx_free(ptr);

	return ptr_snmp;
#undef MIN_TEMPLATE_NAME_LEN
}

char	*zbx_dbpatch_make_trigger_function(const char *name, const char *tpl, const char *key, const char *param)
{
	char	*template_name, *func = NULL;
	size_t	func_alloc = 0, func_offset = 0;

	template_name = zbx_strdup(NULL, tpl);
	template_name = zbx_update_template_name(template_name);

	zbx_snprintf_alloc(&func, &func_alloc, &func_offset, "%s(/%s/%s", name, template_name, key);

	if ('$' == *param && ',' == *++param)
		param++;

	if ('\0' != *param)
		zbx_snprintf_alloc(&func, &func_alloc, &func_offset, ",%s", param);

	zbx_chrcpy_alloc(&func, &func_alloc, &func_offset, ')');

	zbx_free(template_name);

	return func;
}

int zbx_compose_trigger_expression(DB_ROW row, zbx_uint64_t rules, char **composed_expr)
{
	char		*trigger_expr;
	int		i;
	DB_ROW		row2;
	DB_RESULT	result2;

	for (i = 0; i < 2; i++)
	{
		int			j;
		char			*error = NULL;
		zbx_eval_context_t	ctx;

		trigger_expr = row[i + 2];

		if ('\0' == *trigger_expr)
		{
			if (0 == i)
			{
				zabbix_log(LOG_LEVEL_WARNING, "%s: empty expression for trigger %s",
						__func__, row[0]);
			}
			continue;
		}

		if (FAIL == zbx_eval_parse_expression(&ctx, trigger_expr, rules, &error))
		{
			zabbix_log(LOG_LEVEL_CRIT, "%s: error parsing trigger expression for %s: %s",
					__func__, row[0], error);
			zbx_free(error);
			return FAIL;
		}

		for (j = 0; j < ctx.stack.values_num; j++)
		{
			zbx_eval_token_t	*token = &ctx.stack.values[j];
			zbx_uint64_t		functionid;

			if (ZBX_EVAL_TOKEN_FUNCTIONID != token->type)
				continue;

			if (SUCCEED != zbx_is_uint64_n(ctx.expression + token->loc.l + 1,
					token->loc.r - token->loc.l - 1, &functionid))
			{
				zabbix_log(LOG_LEVEL_CRIT, "%s: error parsing trigger expression %s,"
						" zbx_is_uint64_n error", __func__, row[0]);
				return FAIL;
			}

			result2 = DBselect(
					"select h.host,i.key_,f.name,f.parameter"
					" from functions f"
					" join items i on i.itemid=f.itemid"
					" join hosts h on h.hostid=i.hostid"
					" where f.functionid=" ZBX_FS_UI64,
					functionid);

			if (NULL != (row2 = DBfetch(result2)))
			{
				char	*func;

				func = zbx_dbpatch_make_trigger_function(row2[2], row2[0], row2[1], row2[3]);
				zbx_variant_clear(&token->value);
				zbx_variant_set_str(&token->value, func);
			}

			DBfree_result(result2);
		}

		zbx_eval_compose_expression(&ctx, &composed_expr[i]);
		zbx_eval_clear(&ctx);
	}

	return SUCCEED;
}

#endif /*HAVE_SQLITE3*/
