/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
**  mod_define.c - Apache module for configuration defines (~{xxx})
**
**  Copyright (c) 2026 Jean-Francois Stenuit
**  Copyright (c) 2011 W. Kleedorfer <wkleedorfer@gmail.com>
**  Copyright (c) 2006 Rainer Jung <rainer.jung@kippdata.de>
**  Copyright (c) 1998-2000 Ralf S. Engelschall <rse@engelschall.com>
**  Copyright (c) 1998-2000 Christian Reiber <chrei@en.muc.de>
**
*/

/*
 *  HISTORY
 *
 *  v1.0: Originally written in December 1998 by
 *        Ralf S. Engelschall <rse@engelschall.com> and
 *        Christian Reiber <chrei@en.muc.de>
 *
 *  v1.1: Completely Overhauled in August 1999 by
 *        Ralf S. Engelschall <rse@engelschall.com>
 *
 *  v2.0: Ported for Apache 2.0 and 2.2 in August 2006 by
 *        Rainer Jung <rainer.jung@kippdata.de>
 *
 *  v2.1: Minor fixes in October 2006 by
 *        Rainer Jung <rainer.jung@kippdata.de>
 *
 *  v2.2: Added file scope to the variables in Feb. 2011
 *        W. Kleedorfer <wkleedorfer@gmail.com>
 *
 *  v4.0: Rewritten for Apache 2.4 in October 2026 by
 *        Jean-Francois Stenuit: DefineStr and ~{xxx} instead of
 *        Define and ${xxx}, which core httpd 2.4 now uses; global
 *        variables, last value wins; empty values; limits on nesting
 *        and on the length of a substituted line
 */

#include "httpd.h"
#include "http_config.h"
#include "http_core.h"
#include "http_log.h"
#include "http_main.h"
#include "util_varbuf.h"

#include "apr_strings.h"
#include "apr_lib.h"

module AP_MODULE_DECLARE_DATA define_module;

APLOG_USE_MODULE(define);

/*
 * Default meta characters and limits
 */
#define DEFAULT_MC_ESCAPE      '\\'
#define DEFAULT_MC_DELIM       '~'
#define DEFAULT_MC_BRACEOPEN   '{'
#define DEFAULT_MC_BRACECLOSE  '}'
#define DEFAULT_MAX_RECURSION  200
#define DEFAULT_MAX_STRING     16000

/*
 * Module state. It lives as long as the configuration pool and is
 * reset by DefineCleanup() when the configuration is read again.
 */
static apr_table_t *tDefines   = NULL;   /* global table of defines */
static char cEscape            = DEFAULT_MC_ESCAPE;
static char cDelim             = DEFAULT_MC_DELIM;
static char cBraceOpen         = DEFAULT_MC_BRACEOPEN;
static char cBraceClose        = DEFAULT_MC_BRACECLOSE;
static int  nMaxRecursion      = DEFAULT_MAX_RECURSION;
static int  nMaxString         = DEFAULT_MAX_STRING;

/*
 * Scanner states for DefineExpand()
 */
typedef enum {
    SS_NONE, SS_SKIP, SS_DELIM
} ScanState;

/*
 * Characters allowed in a variable name
 */
static int DefineIsIdChar(char c)
{
    return apr_isalnum(c) || c == '_' || c == ':' || c == '-';
}

/*
 * Report a reference that is left as it is. These are errors in the
 * configuration, but not fatal ones: the server still starts.
 */
static void DefineWarn(apr_pool_t *p, const ap_directive_t *dir,
                       const char *cpMsg)
{
    ap_log_perror(APLOG_MARK, APLOG_ERR, 0, p,
                  "mod_define: %s, left as it is: %s, line %d",
                  cpMsg, dir->filename, dir->line_num);
}

static const char *DefineTooLong(apr_pool_t *p)
{
    return apr_psprintf(p, "line longer than %d characters after "
                        "substitution (see DefineMaxString)", nMaxString);
}

/*
 * Append cpIn to vb, replacing every ~{name} by the value of name.
 * A value is expanded the same way before it is inserted, one level
 * deeper. The escape character protects the character after it; both
 * are copied as they are. Returns NULL, or a message for an error that
 * must stop the server.
 */
static const char *DefineExpand(apr_pool_t *p, const ap_directive_t *dir,
                                struct ap_varbuf *vb, const char *cpIn,
                                int nDepth)
{
    const char *cp;
    const char *cpCopied;   /* start of the text not copied yet */
    const char *cpName;
    const char *cpEnd;
    const char *cpVal;
    const char *cpError;
    char *cpVar;
    ScanState s = SS_NONE;

    cpCopied = cpIn;
    for (cp = cpIn; *cp != '\0'; cp++) {
        switch (s) {
            case SS_NONE:
                if (*cp == cEscape)
                    s = SS_SKIP;
                else if (*cp == cDelim)
                    s = SS_DELIM;
                continue;
            case SS_SKIP:
                s = SS_NONE;
                continue;
            case SS_DELIM:
                if (*cp == cEscape)
                    s = SS_SKIP;
                else if (*cp != cBraceOpen)
                    s = SS_NONE;
                else
                    break;
                continue;
        }

        /* cp is on the brace after the delimiter: read the name */
        s = SS_NONE;
        cpName = cp + 1;
        for (cpEnd = cpName; DefineIsIdChar(*cpEnd); cpEnd++)
            ;
        if (*cpEnd != cBraceClose || cpEnd == cpName) {
            if (*cpEnd == '\0')
                DefineWarn(p, dir, "unterminated variable reference");
            else if (*cpEnd == cBraceClose)
                DefineWarn(p, dir, "empty variable reference");
            else
                DefineWarn(p, dir, apr_psprintf(p, "illegal character '%c' "
                                                "in variable reference",
                                                *cpEnd));
            continue;
        }
        cpVar = apr_pstrmemdup(p, cpName, cpEnd - cpName);
        cpVal = tDefines ? apr_table_get(tDefines, cpVar) : NULL;
        if (cpVal == NULL) {
            DefineWarn(p, dir, apr_psprintf(p, "variable '%s' not defined",
                                            cpVar));
            cp = cpEnd;
            continue;
        }
        if (nDepth >= nMaxRecursion)
            return apr_psprintf(p, "more than %d nested variables while "
                                "expanding '%s' (does it refer to itself?)",
                                nMaxRecursion, cpVar);

        /* copy the text before the reference, then the expanded value */
        ap_varbuf_strmemcat(vb, cpCopied, (int)(cp - 1 - cpCopied));
        if ((cpError = DefineExpand(p, dir, vb, cpVal, nDepth + 1)) != NULL)
            return cpError;
        if (vb->strlen > (apr_size_t)nMaxString)
            return DefineTooLong(p);
        cp = cpEnd;
        cpCopied = cpEnd + 1;
    }
    ap_varbuf_strmemcat(vb, cpCopied, (int)strlen(cpCopied));
    if (vb->strlen > (apr_size_t)nMaxString)
        return DefineTooLong(p);
    return NULL;
}

/*
 * Substitute the variables in the arguments of every directive of the
 * tree, sections included. Returns NULL, or a message for an error that
 * must stop the server.
 */
static const char *DefineWalkConfig(apr_pool_t *pconf, apr_pool_t *ptemp,
                                    ap_directive_t *current)
{
    struct ap_varbuf vb;
    const char *cpError;

    for (; current != NULL; current = current->next) {
        if (current->args != NULL && strchr(current->args, cDelim) != NULL) {
            ap_varbuf_init(ptemp, &vb, strlen(current->args) + 1);
            cpError = DefineExpand(ptemp, current, &vb, current->args, 0);
            if (cpError != NULL)
                return apr_psprintf(pconf, "mod_define: %s: %s, line %d",
                                    cpError, current->filename,
                                    current->line_num);
            if (strcmp(vb.buf, current->args) != 0)
                current->args = apr_pstrmemdup(pconf, vb.buf, vb.strlen);
            ap_varbuf_free(&vb);
        }
        if (current->first_child != NULL) {
            cpError = DefineWalkConfig(pconf, ptemp, current->first_child);
            if (cpError != NULL)
                return cpError;
        }
    }
    return NULL;
}

/*
 * Called once the whole configuration has been read, before the server
 * processes it: the values are final, so they apply everywhere.
 */
static int DefinePreConfig(apr_pool_t *pconf, apr_pool_t *plog,
                           apr_pool_t *ptemp)
{
    const char *cpError = NULL;

    if (cEscape == cDelim || cEscape == cBraceOpen || cEscape == cBraceClose
        || cDelim == cBraceOpen || cDelim == cBraceClose
        || cBraceOpen == cBraceClose)
        cpError = "mod_define: the four meta characters must differ";
    else
        cpError = DefineWalkConfig(pconf, ptemp, ap_conftree);
    if (cpError != NULL) {
        ap_log_perror(APLOG_MARK, APLOG_CRIT, 0, plog, "%s", cpError);
        return HTTP_INTERNAL_SERVER_ERROR;
    }
    return OK;
}

/*
 * Module Cleanup
 */
static apr_status_t DefineCleanup(void *data)
{
    /* reset private variables when config pool is cleared */
    tDefines      = NULL;
    cEscape       = DEFAULT_MC_ESCAPE;
    cDelim        = DEFAULT_MC_DELIM;
    cBraceOpen    = DEFAULT_MC_BRACEOPEN;
    cBraceClose   = DEFAULT_MC_BRACECLOSE;
    nMaxRecursion = DEFAULT_MAX_RECURSION;
    nMaxString    = DEFAULT_MAX_STRING;
    return APR_SUCCESS;
}

/*
 * Module Initialization, on the first directive of the module
 */
static void DefineInit(apr_pool_t *p)
{
    if (tDefines != NULL)
        return;
    tDefines = apr_table_make(p, 64);
    apr_pool_cleanup_register(p, NULL, DefineCleanup, apr_pool_cleanup_null);
}

/*
 * Implementation of the `DefineStr' configuration directive. The value
 * is stored as it is: references in it are expanded where it is used.
 */
static const char *cmd_definestr(cmd_parms *cmd, void *config,
                                 const char *cpVar, const char *cpVal)
{
    const char *cp;

    if (*cpVar == '\0')
        return "DefineStr: empty variable name";
    for (cp = cpVar; *cp != '\0'; cp++)
        if (!DefineIsIdChar(*cp))
            return apr_psprintf(cmd->pool, "DefineStr: illegal character "
                                "'%c' in variable name '%s'", *cp, cpVar);
    DefineInit(cmd->pool);
    apr_table_set(tDefines, cpVar, cpVal != NULL ? cpVal : "");
    return NULL;
}

/*
 * Implementation of the `DefineMeta*' directives: cmd->info points to
 * the meta character to set
 */
static const char *cmd_meta(cmd_parms *cmd, void *config, const char *cpArg)
{
    if (cpArg[0] == '\0' || cpArg[1] != '\0' || DefineIsIdChar(cpArg[0])
        || apr_isspace(cpArg[0]))
        return apr_pstrcat(cmd->pool, cmd->cmd->name, " takes one character, "
                           "which cannot be a letter, a digit, '_', ':', '-' "
                           "or a space", NULL);
    DefineInit(cmd->pool);
    *(char *)cmd->info = cpArg[0];
    return NULL;
}

/*
 * Implementation of the `DefineMaxRecursion' and `DefineMaxString'
 * directives: cmd->info points to the limit to set
 */
static const char *cmd_limit(cmd_parms *cmd, void *config, const char *cpArg)
{
    char *cpEnd;
    apr_int64_t n;

    n = apr_strtoi64(cpArg, &cpEnd, 10);
    if (*cpArg == '\0' || *cpEnd != '\0' || n < 1 || n > 100000000)
        return apr_pstrcat(cmd->pool, cmd->cmd->name, " takes a number "
                           "between 1 and 100000000", NULL);
    DefineInit(cmd->pool);
    *(int *)cmd->info = (int)n;
    return NULL;
}

/*
 * Feature names for <IfDefine>, set when the module is loaded
 */
static void DefineAddFeature(const char *cpName)
{
    if (ap_server_config_defines == NULL || ap_exists_config_define(cpName))
        return;
    *(const char **)apr_array_push(ap_server_config_defines) =
        apr_pstrdup(ap_server_config_defines->pool, cpName);
}

/*
 * Module Directive lists
 */
static const command_rec DefineDirectives[] = {
    AP_INIT_TAKE12("DefineStr", cmd_definestr, NULL,
      RSRC_CONF|ACCESS_CONF|EXEC_ON_READ,
      "Define a configuration variable (string), referenced as ~{name}"),
    AP_INIT_TAKE1("DefineMaxRecursion", cmd_limit, &nMaxRecursion,
      RSRC_CONF|EXEC_ON_READ, "Maximum number of nested variables"),
    AP_INIT_TAKE1("DefineMaxString", cmd_limit, &nMaxString,
      RSRC_CONF|EXEC_ON_READ,
      "Maximum length of a line after substitution"),
    AP_INIT_TAKE1("DefineMetaEscape", cmd_meta, &cEscape,
      RSRC_CONF|EXEC_ON_READ, "Escape character (default \\)"),
    AP_INIT_TAKE1("DefineMetaDelim", cmd_meta, &cDelim,
      RSRC_CONF|EXEC_ON_READ, "Delimiter character (default ~)"),
    AP_INIT_TAKE1("DefineMetaBraceOpen", cmd_meta, &cBraceOpen,
      RSRC_CONF|EXEC_ON_READ, "Opening brace character (default {)"),
    AP_INIT_TAKE1("DefineMetaBraceClose", cmd_meta, &cBraceClose,
      RSRC_CONF|EXEC_ON_READ, "Closing brace character (default })"),
    { NULL }
};

static void define_register_hooks(apr_pool_t *p)
{
    DefineAddFeature("MODDEFINE_EmptyValue");
    DefineAddFeature("MODDEFINE_MaxRecursion");
    DefineAddFeature("MODDEFINE_MaxString");
    ap_hook_pre_config(DefinePreConfig, NULL, NULL, APR_HOOK_REALLY_FIRST);
}

module AP_MODULE_DECLARE_DATA define_module = {
    STANDARD20_MODULE_STUFF,
    NULL,                 /* create per-dir    config structures */
    NULL,                 /* merge  per-dir    config structures */
    NULL,                 /* create per-server config structures */
    NULL,                 /* merge  per-server config structures */
    DefineDirectives,     /* table of config file commands       */
    define_register_hooks /* register hooks */
};
