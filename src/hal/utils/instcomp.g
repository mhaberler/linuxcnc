#!/usr/bin/python
#    This is 'comp', a tool to write HAL boilerplate
#    Copyright 2006 Jeff Epler <jepler@unpythonic.net>
#
#    This program is free software; you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation; either version 2 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with this program; if not, write to the Free Software
#    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

# paramdir and array removed 1049 13032015
# passes int values and assigns in .c file

import os, sys, tempfile, shutil, getopt, time
BASE = os.path.abspath(os.path.join(os.path.dirname(sys.argv[0]), ".."))
sys.path.insert(0, os.path.join(BASE, "lib", "python"))

%%
parser Hal:
    ignore: "//.*"
    ignore: "/[*](.|\n)*?[*]/"
    ignore: "[ \t\r\n]+"

    token END: ";;"
    token PARAMDIRECTION: "rw|r"
    token PINDIRECTION: "in|out|io"
    token TYPE: "float|bit|signed|unsigned|u32|s32"
    token MPTYPE: "int|string"
    token NAME: "[a-zA-Z_][a-zA-Z0-9_]*"
    token STARREDNAME: "[*]*[a-zA-Z_][a-zA-Z0-9_]*"
    token HALNAME: "[#a-zA-Z_][-#a-zA-Z0-9_.]*"
    token FPNUMBER: "-?([0-9]*\.[0-9]+|[0-9]+\.?)([Ee][+-]?[0-9]+)?f?"
    token NUMBER: "0x[0-9a-fA-F]+|[+-]?[0-9]+"
    token STRING: "\"(\\.|[^\\\"])*\""
    token HEADER: "<.*?>"
    token POP: "[-()+*/]|&&|\\|\\||personality|==|&|!=|<|<=|>|>="
    token TSTRING: "\"\"\"(\\.|\\\n|[^\\\"]|\"(?!\"\")|\n)*\"\"\""

    rule File: ComponentDeclaration Declaration* "$" {{ return True }}
    rule ComponentDeclaration:
        "component" NAME OptString";" {{ comp(NAME, OptString); }}
    rule Declaration:
        "pin" PINDIRECTION TYPE HALNAME OptArray OptSAssign OptString ";"  {{ pin(HALNAME, TYPE, OptArray, PINDIRECTION, OptString, OptSAssign) }}
      | "param" PARAMDIRECTION TYPE HALNAME OptArray OptSAssign OptString ";" {{ param(HALNAME, TYPE, OptArray, PARAMDIRECTION, OptString, OptSAssign) }}
      | "instanceparam" MPTYPE HALNAME OptSAssign OptString ";" {{ instanceparam(HALNAME, MPTYPE, OptString, OptSAssign) }}
      | "moduleparam" MPTYPE HALNAME OptSAssign OptString ";" {{ moduleparam(HALNAME, MPTYPE, OptString, OptSAssign) }}
      | "function" NAME OptFP OptString ";"       {{ function(NAME, OptFP, OptString) }}
      | "variable" NAME STARREDNAME OptSimpleArray OptAssign ";" {{ variable(NAME, STARREDNAME, OptSimpleArray, OptAssign) }}
      | "option" NAME OptValue ";"   {{ option(NAME, OptValue) }}
      | "see_also" String ";"   {{ see_also(String) }}
      | "notes" String ";"   {{ notes(String) }}
      | "description" String ";"   {{ description(String) }}
      | "license" String ";"   {{ license(String) }}
      | "author" String ";"   {{ author(String) }}
      | "include" Header ";"   {{ include(Header) }}
      | "modparam" NAME {{ NAME1=NAME; }} NAME OptSAssign OptString ";" {{ modparam(NAME1, NAME, OptSAssign, OptString) }}

    rule Header: STRING {{ return STRING }} | HEADER {{ return HEADER }}

    rule String: TSTRING {{ return eval(TSTRING) }}
            | STRING {{ return eval(STRING) }}

    rule OptSimpleArray: "\[" NUMBER "\]" {{ return int(NUMBER) }}
            | {{ return 0 }}

    rule OptArray: "\[" NUMBER "\]" {{ return int(NUMBER) }}
            | {{ return 0 }}

    rule OptString: TSTRING {{ return eval(TSTRING) }}
            | STRING {{ return eval(STRING) }}
            | {{ return '' }}

    rule OptAssign: "=" Value {{ return Value; }}
                | {{ return None }}

    rule OptSAssign: "=" SValue {{ return SValue; }}
                | {{ return None }}

    rule OptFP: "fp" {{ return 1 }} | "nofp" {{ return 0 }} | {{ return 1 }}

    rule Value: "yes" {{ return 1 }} | "no" {{ return 0 }}
                | "true" {{ return 1 }} | "false" {{ return 0 }}
                | "TRUE" {{ return 1 }} | "FALSE" {{ return 0 }}
                | NAME {{ return NAME }}
                | FPNUMBER {{ return float(FPNUMBER.rstrip("f")) }}
                | NUMBER {{ return int(NUMBER,0) }}

    rule SValue: "yes" {{ return "yes" }} | "no" {{ return "no" }}
                | "true" {{ return "true" }} | "false" {{ return "false" }}
                | "TRUE" {{ return "TRUE" }} | "FALSE" {{ return "FALSE" }}
                | NAME {{ return NAME }}
                | FPNUMBER {{ return FPNUMBER }}
                | NUMBER {{ return NUMBER }}
                | STRING {{ return STRING}}

    rule OptValue: Value {{ return Value }}
                | {{ return 1 }}

    rule OptSValue: SValue {{ return SValue }}
                | {{ return 1 }}
%%

mp_decl_map = {'int': 'RTAPI_MP_INT', 'dummy': None}

# These are symbols that comp puts in the global namespace of the C file it
# creates.  The user is thus not allowed to add any symbols with these
# names.  That includes not only global variables and functions, but also
# HAL pins & parameters, because comp adds #defines with the names of HAL
# pins & params.
reserved_names = [ 'comp_id', 'fperiod', 'rtapi_app_main', 'rtapi_app_exit', 'extra_setup', 'extra_cleanup' ]

global intparams
global strparams

def _parse(rule, text, filename=None):
    global P, S
    S = HalScanner(text, filename=filename)
    P = Hal(S)
    return runtime.wrap_error_reporter(P, rule)

def parse(filename):
    initialize()
    f = open(filename).read()
    a, b = f.split("\n;;\n", 1)
    p = _parse('File', a + "\n\n", filename)
    if not p: raise SystemExit, 1
    if require_license:
        if not finddoc('license'):
            raise SystemExit, "%s:0: License not specified" % filename
    return a, b

dirmap = {'r': 'HAL_RO', 'rw': 'HAL_RW', 'in': 'HAL_IN', 'out': 'HAL_OUT', 'io': 'HAL_IO' }
typemap = {'signed': 's32', 'unsigned': 'u32'}
deprmap = {'s32': 'signed', 'u32': 'unsigned'}
deprecated = ['s32', 'u32']

def initialize():
    global functions, params, instanceparams, moduleparams, pins, options, comp_name, names, docs, variables
    global modparams, includes

    functions = []; params = []; instanceparams = []; moduleparams = []; pins = []; options = {}; variables = []
    modparams = []; docs = []; includes = [];
    comp_name = None

    names = {}

def Warn(msg, *args):
    if args:
        msg = msg % args
    print >>sys.stderr, "%s:%d: Warning: %s" % (S.filename, S.line, msg)

def Error(msg, *args):
    if args:
        msg = msg % args
    raise runtime.SyntaxError(S.get_pos(), msg, None)

def comp(name, doc):
    docs.append(('component', name, doc))
    global comp_name
    if comp_name:
        Error("Duplicate specification of component name")
    comp_name = name;

def description(doc):
    docs.append(('descr', doc));

def license(doc):
    docs.append(('license', doc));

def author(doc):
    docs.append(('author', doc));

def see_also(doc):
    docs.append(('see_also', doc));

def notes(doc):
    docs.append(('notes', doc));

def type2type(type):
    # When we start warning about s32/u32 this is where the warning goes
    return typemap.get(type, type)

def checkarray(name, array):
    hashes = len(re.findall("#+", name))
    if array:
        if hashes == 0: Error("Array name contains no #: %r" % name)
        if hashes > 1: Error("Array name contains more than one block of #: %r" % name)
    else:
        if hashes > 0: Error("Non-array name contains #: %r" % name)

def check_name_ok(name):
    if name in reserved_names:
        Error("Variable name %s is reserved" % name)
    if name in names:
        Error("Duplicate item name %s" % name)

def pin(name, type, array, dir, doc, value):
    checkarray(name, array)
    type = type2type(type)
    check_name_ok(name)
    docs.append(('pin', name, type, array, dir, doc, value))
    names[name] = None
    pins.append((name, type, array, dir, value))

def param(name, type, array, dir, doc, value):
    checkarray(name, array)
    type = type2type(type)
    check_name_ok(name)
    docs.append(('param', name, type, array, dir, doc, value))
    names[name] = None
    params.append((name, type, array, dir, value))

def instanceparam(name, type, doc, value):
#    checkarray(name, array)
    type = type2type(type)
    check_name_ok(name)
    docs.append(('instanceparam', name, type, doc, value))
    names[name] = None
    instanceparams.append((name, type, value))

def moduleparam(name, type, doc, value):
#    checkarray(name, array)
    type = type2type(type)
    check_name_ok(name)
    docs.append(('moduleparam', name, type, doc, value))
    names[name] = None
    moduleparams.append((name, type, value))

def function(name, fp, doc):
    check_name_ok(name)
    docs.append(('funct', name, fp, doc))
    names[name] = None
    functions.append((name, fp))

def option(name, value):
    if name in options:
        Error("Duplicate option name %s" % name)
    options[name] = value

def variable(type, name, array, default):
    check_name_ok(name)
    names[name] = None
    variables.append((type, name, array, default))

def modparam(type, name, default, doc):
    check_name_ok(name)
    names[name] = None
    modparams.append((type, name, default, doc))

def include(value):
    includes.append((value))

def removeprefix(s,p):
    if s.startswith(p): return s[len(p):]
    return s

def to_hal(name):
    name = re.sub("#+", lambda m: "%%0%dd" % len(m.group(0)), name)
    return name.replace("_", "-").rstrip("-").rstrip(".")
def to_c(name):
    name = re.sub("[-._]*#+", "", name)
    name = name.replace("#", "").replace(".", "_").replace("-", "_")
    return re.sub("_+", "_", name)

##################### Start ########################################
def prologue(f):
    print >> f, "/* Autogenerated by %s on %s -- do not edit */" % (
        sys.argv[0], time.asctime())
    print >> f, """\
#include "rtapi.h"
#ifdef RTAPI
#include "rtapi_app.h"
#endif
#include "rtapi_string.h"
#include "rtapi_errno.h"
#include "hal.h"
#include "hal_priv.h"

static int comp_id;
"""

    print >>f, "static char *compname = \"%s\";\n" % (comp_name)

    for name in includes:
        print >>f, "#include %s" % name

    names = {}

    def q(s):
        s = s.replace("\\", "\\\\")
        s = s.replace("\"", "\\\"")
        s = s.replace("\r", "\\r")
        s = s.replace("\n", "\\n")
        s = s.replace("\t", "\\t")
        s = s.replace("\v", "\\v")
        return '"%s"' % s

    print >>f, "#ifdef MODULE_INFO"
    for v in docs:
        if not v: continue
        v = ":".join(map(str, v))
        print >>f, "MODULE_INFO(linuxcnc, %s);" % q(v)
        license = finddoc('license')
    if license and license[1]:
        print >>f, "MODULE_LICENSE(\"%s\");" % license[1].split("\n")[0]
    print >>f, "#endif // MODULE_INFO"
    print >>f


    has_data = options.get("data")

    has_array = False
    for name, type, array, dir, value in pins:
        if array: has_array = True
    for name, type, array, dir, value in params:
        if array: has_array = True

#    for name, type, array, dir, value in instanceparams:
#        if array: has_array = True

#    for name, type, array, dir, value in moduleparams:
#        if array: has_array = True

    for type, name, default, doc in modparams:
        decl = mp_decl_map[type]
        if decl:
            print >>f, "%s %s" % (type, name),
            if default: print >>f, "= %s;" % default
            else: print >>f, ";"
            print >>f, "%s(%s, %s);" % (decl, name, q(doc))

    print >>f

################# struct declaration ##########################

    print >>f, "struct inst_data {"

    for name, type, array, dir, value in pins:
        if array:
            if isinstance(array, tuple): array = array[0]
            print >>f, "    hal_%s_t *%s[%s];" % (type, to_c(name), array)
        else:
            print >>f, "    hal_%s_t *%s;" % (type, to_c(name))
        names[name] = 1

    for name, type, array, dir, value in params:
        if array:
            if isinstance(array, tuple): array = array[0]
            print >>f, "    hal_%s_t %s[%s];" % (type, to_c(name), array)
        else:
            print >>f, "    hal_%s_t %s;" % (type, to_c(name))
        names[name] = 1



    for type, name, array, value in variables:
        if array:
            print >>f, "    %s %s[%d];\n" % (type, name, array)
        else:
            print >>f, "    %s %s;\n" % (type, name)
    if has_data:
        print >>f, "    void *_data;"

    print >>f, "};"

############## extra headers and forward defines of functions  ##########################

    if options.get("userspace"):
        print >>f, "#include <stdlib.h>"

    print >>f
    for name, fp in functions:
        if names.has_key(name):
            Error("Duplicate item name: %s" % name)
        print >>f, "static void %s(void *arg, long period);\n" % to_c(name)
        names[name] = 1
    data = options.get('data')
    if(data) :
        print >>f, "static int __comp_get_data_size(void);\n"

    print >>f, "static int instantiate(const char *name, const int argc, const char**argv);\n"

    print >>f, "static int delete(const char *name, void *inst, const int inst_size);\n"

    if options.get("extra_setup"):
        print >>f, "static int extra_setup(struct inst_data* ip, char *name, long extra_arg);"
    if options.get("extra_cleanup"):
        print >>f, "static void extra_cleanup(void);"

    if not options.get("no_convenience_defines"):
        print >>f, "#undef TRUE"
        print >>f, "#define TRUE (1)"
        print >>f, "#undef FALSE"
        print >>f, "#define FALSE (0)"
        print >>f, "#undef true"
        print >>f, "#define true (1)"
        print >>f, "#undef false"
        print >>f, "#define false (0)"

    print >>f

###########################  export_halobjs()  ######################################################

    print >>f, "static int export_halobjs(struct inst_data *ip, int owner_id, const char *name)\n{"
    if len(functions) > 1:
        print >>f, "    char buf[HAL_NAME_LEN + 1];"
    print >>f, "    int r = 0;"
    if has_array:
        print >>f, "    int j = 0;"
    if has_data:
        print >>f, "    ip->_data = (char*)ip + sizeof(struct inst_data);"
    if options.get("extra_setup"):
        print >>f, "    r = extra_setup(ip, name, 0L);"
	print >>f, "    if(r != 0) return r;"

    for name, type, array, dir, value in pins:
        if array:
            if isinstance(array, tuple): array = array[1]
            print >>f, "    for(j=0; j < (%s); j++) {" % array
            print >>f, "        r = hal_pin_%s_newf(%s, &(ip->%s[j]), owner_id," % (
                type, dirmap[dir], to_c(name))
            print >>f, "            \"%%s%s\", name, j);" % to_hal("." + name)
            print >>f, "        if(r != 0) return r;"
            if value is not None:
                print >>f, "    *(ip->%s[j]) = %s;" % (to_c(name), value)
            print >>f, "    }"
        else:
            print >>f, "    r = hal_pin_%s_newf(%s, &(ip->%s), owner_id," % (
                type, dirmap[dir], to_c(name))
            print >>f, "        \"%%s%s\", name);" % to_hal("." + name)
            print >>f, "    if(r != 0) return r;"
            if value is not None:
                print >>f, "    *(ip->%s) = %s;" % (to_c(name), value)

    for name, type, array, dir, value in params:
        if array:
            if isinstance(array, tuple): array = array[1]
            print >>f, "    for(j=0; j < %s; j++) {" % array
            print >>f, "        r = hal_param_%s_newf(%s, &(ip->%s[j]), owner_id," % (
                type, dirmap[dir], to_c(name))
            print >>f, "            \"%%s%s\", name, j);" % to_hal("." + name)
            print >>f, "        if(r != 0) return r;"
            if value is not None:
                print >>f, "    ip->%s[j] = %s;" % (to_c(name), value)
            print >>f, "    }"
        else:
            print >>f, "    r = hal_param_%s_newf(%s, &(ip->%s), owner_id," % (
                type, dirmap[dir], to_c(name))
            print >>f, "        \"%%s%s\", name);" % to_hal("." + name)
            if value is not None:
                print >>f, "    ip->%s = %s;" % (to_c(name), value)
            print >>f, "    if(r != 0) return r;"

    for type, name, array, value in variables:
        if value is None: continue
        if array:
            print >>f, "    for(j=0; j < %s; j++) {" % array
            print >>f, "        ip->%s[j] = %s;" % (name, value)
            print >>f, "    }"
        else:
            print >>f, "    ip->%s = %s;" % (name, value)

    for name, fp in functions:
        strg = "    r = hal_export_functf(%s, ip, 0, 0, owner_id," % (to_c(name))
        strg +=  "\"%s.funct\", name);"
        print >>f, strg
        print >>f, "    if(r != 0) return r;"

    print >>f, "    return 0;"
    print >>f, "}"

############################  RTAPI_IP / MP declarations ########################################################
    if not options.get("userspace"):
        print >>f
        intparams = 0
        strparams = 0
        for name, mptype, value in instanceparams:
            if (mptype == 'int'):
                if value == None: v = 0
                else: v = value
                print >>f, "static %s %s = %d;" % (mptype, to_c(name), int(v))
                print >>f, "RTAPI_IP_INT(%s, \"Instance integer param '%s'\");\n" % (to_c(name), to_c(name))
#                intparams += 1
            else:
                if value == None: strng = "\"\\0\"";
                else: strng = value
                print >>f, "static char *%s = %s;" % (to_c(name), strng)
                print >>f, "RTAPI_IP_STRING(%s, \"Instance string param '%s'\");\n" % (to_c(name), to_c(name))
#                strparams += 1
####  Not sure if these will be required for indexing - take out for now ####
#        if (intparams or strparams) :
#            print >>f, "// instance param counters for use in indexing"
#            if intparams:
#                print >>f, "static int intparams = %d;" % (intparams)
#            if strparams:
#                print >>f, "static int strparams = %d;\n" % (strparams)
##############################################################################

        for name, mptype, value in moduleparams:
            if (mptype == 'int'):
                if value == None: v = 0
                else: v = value
                print >>f, "static %s %s = %d;" % (mptype, to_c(name), int(v))
                print >>f, "RTAPI_MP_INT(%s, \"Module integer param '%s'\");\n" % (to_c(name), to_c(name))
#                intparams += 1
            else:
                if value == None: strng = "\"\\0\"";
                else: strng = value
                print >>f, "static char *%s = %s;" % (to_c(name), strng)
                print >>f, "RTAPI_MP_STRING(%s, \"Module string param '%s'\");\n" % (to_c(name), to_c(name))
#                strparams += 1

###########################  instantiate() ###############################################################

    print >>f, "\n// constructor - init all HAL pins, params, funct etc here"
    print >>f, "static int instantiate(const char *name, const int argc, const char**argv)\n{"
    print >>f, "struct inst_data *ip;\n"

    print >>f, "// allocate a named instance, and some HAL memory for the instance data"
    print >>f, "int inst_id = hal_inst_create(name, comp_id, sizeof(struct inst_data), (void **)&ip);\n"

    print >>f, "    if (inst_id < 0)\n        return -1;\n"

    print >>f, "// here ip is guaranteed to point to a blob of HAL memory of size sizeof(struct inst_data)."
    print >>f, "    hal_print_msg(RTAPI_MSG_ERR,\"%s inst=%s argc=%d\",__FUNCTION__, name, argc);\n"
    print >>f, "// Debug print of params and values"
    for name, mptype, value in instanceparams:
        if (mptype == 'int'):
            strg = "    hal_print_msg(RTAPI_MSG_ERR,\"%s: int instance param: %s=%d\",__FUNCTION__,"
            strg += "\"%s\", %s);" % (to_c(name), to_c(name))
            print >>f, strg
        else:
            strg = "    hal_print_msg(RTAPI_MSG_ERR,\"%s: string instance param: %s=%s\",__FUNCTION__,"
            strg += "\"%s\", %s);" % (to_c(name), to_c(name))
            print >>f, strg

    for name, mptype, value in moduleparams:
        if (mptype == 'int'):
            strg = "    hal_print_msg(RTAPI_MSG_ERR,\"%s: int module param: %s=%d\",__FUNCTION__,"
            strg += "\"%s\", %s);" % (to_c(name), to_c(name))
            print >>f, strg
        else:
            strg = "    hal_print_msg(RTAPI_MSG_ERR,\"%s: string module param: %s=%s\",__FUNCTION__,"
            strg += "\"%s\", %s);" % (to_c(name), to_c(name))
            print >>f, strg

    print >>f, "\n// these pins - params - functs will be owned by the instance, and can be separately exited"
    print >>f, "    return export_halobjs(ip, inst_id, name);\n}"

##############################  rtapi_app_main  ######################################################

    print >>f, "\nint rtapi_app_main(void)\n{"
    print >>f, "// Debug print of params and values"
    for name, mptype, value in instanceparams:
        if (mptype == 'int'):
            strg = "    hal_print_msg(RTAPI_MSG_ERR,\"%s: int instance param: %s=%d\",__FUNCTION__,"
            strg += "\"%s\", %s);" % (to_c(name), to_c(name))
            print >>f, strg
        else:
            strg = "    hal_print_msg(RTAPI_MSG_ERR,\"%s: string instance param: %s=%s\",__FUNCTION__,"
            strg += "\"%s\", %s);" % (to_c(name), to_c(name))
            print >>f, strg

    for name, mptype, value in moduleparams:
        if (mptype == 'int'):
            strg = "    hal_print_msg(RTAPI_MSG_ERR,\"%s: int module param: %s=%d\",__FUNCTION__,"
            strg += "\"%s\", %s);" % (to_c(name), to_c(name))
            print >>f, strg
        else:
            strg = "    hal_print_msg(RTAPI_MSG_ERR,\"%s: string module param: %s=%s\",__FUNCTION__,"
            strg += "\"%s\", %s);" % (to_c(name), to_c(name))
            print >>f, strg

    print >>f, "    // to use default destructor, use NULL instead of delete"
    print >>f, "    comp_id = hal_xinit(TYPE_RT, 0, 0, instantiate, delete, compname);"
    print >>f, "    if (comp_id < 0)"
    print >>f, "        return -1;\n"

    print >>f, "    struct inst_data *ip = hal_malloc(sizeof(struct inst_data));\n"

    print >>f, "    // traditional behavior: these pins/params/functs will be owned by the component"
    print >>f, "    // NB: this 'instance' cannot be exited"
    print >>f, "    if (export_halobjs(ip, comp_id, \"initial\"))"
    print >>f, "        return -1;\n"
    if options.get("extra_cleanup"):
        print >>f, "    extra_cleanup();\n"

    print >>f, "    hal_ready(comp_id);\n"
    print >>f, "    return 0;\n}\n"

    print >>f, "void rtapi_app_exit(void)\n {"
    if options.get("extra_cleanup"):
            print >>f, "    extra_cleanup();"
    print >>f, "    hal_exit(comp_id);"
    print >>f, "}\n"

###########################  user_mainloop()  &  user_init()  ############################################

    if options.get("userspace"):
        print >>f, "static void user_mainloop(void);"
        if options.get("userinit"):
            print >>f, "static void userinit(int argc, char **argv);"
        print >>f, "int argc=0; char **argv=0;"
        print >>f, "int main(int argc_, char **argv_) {"
        print >>f, "    argc = argc_; argv = argv_;"
        print >>f
        if options.get("userinit", 0):
            print >>f, "    userinit(argc, argv);"
        print >>f
        print >>f, "    if(rtapi_app_main() < 0) return 1;"
        print >>f, "    user_mainloop();"
        print >>f, "    rtapi_app_exit();"
        print >>f, "    return 0;"
        print >>f, "}"

#########################   delete()  ####################################################################

    print >>f, "// custom destructor - normally not needed"
    print >>f, "// pins, params, and functs are automatically deallocated regardless if a"
    print >>f, "// destructor is used or not (see below)"
    print >>f, "//"
    print >>f, "// some objects like vtables, rings, threads are not owned by a component"
    print >>f, "// interaction with such objects may require a custom destructor for"
    print >>f, "// cleanup actions"
    print >>f, "// NB: if a customer destructor is used, it is called"
    print >>f, "// - after the instance's functs have been removed from their respective threads"
    print >>f, "//   (so a thread funct call cannot interact with the destructor any more)"
    print >>f, "// - any pins and params of this instance are still intact when the destructor is"
    print >>f, "//   called, and they are automatically destroyed by the HAL library once the"
    print >>f, "//   destructor returns"
    print >>f, "static int delete(const char *name, void *inst, const int inst_size)\n{\n"

    print >>f, "\n    hal_print_msg(RTAPI_MSG_ERR,\"%s inst=%s size=%d %p\\n\", __FUNCTION__, name, inst_size, inst);"
    print >>f, "// Debug print of params and values"
    for name, mptype, value in instanceparams:
        if (mptype == 'int'):
            strg = "    hal_print_msg(RTAPI_MSG_ERR,\"%s: int instance param: %s=%d\",__FUNCTION__,"
            strg += "\"%s\", %s);" % (to_c(name), to_c(name))
            print >>f, strg
        else:
            strg = "    hal_print_msg(RTAPI_MSG_ERR,\"%s: string instance param: %s=%s\",__FUNCTION__,"
            strg += "\"%s\", %s);" % (to_c(name), to_c(name))
            print >>f, strg

    for name, mptype, value in moduleparams:
        if (mptype == 'int'):
            strg = "    hal_print_msg(RTAPI_MSG_ERR,\"%s: int module param: %s=%d\",__FUNCTION__,"
            strg += "\"%s\", %s);" % (to_c(name), to_c(name))
            print >>f, strg
        else:
            strg = "    hal_print_msg(RTAPI_MSG_ERR,\"%s: string module param: %s=%s\",__FUNCTION__,"
            strg += "\"%s\", %s);" % (to_c(name), to_c(name))
            print >>f, strg

    print >>f, "    return 0;\n"
    print >>f, "}\n"

######################  preliminary defines before user FUNCTION(_) ######################################

    print >>f
    print >>f, "struct inst_data *ip;\n"

    print >>f
    if not options.get("no_convenience_defines"):
        print >>f, "#undef FUNCTION"
        print >>f, "#define FUNCTION(name) static void name(void *arg, long period)"
        print >>f, "#undef EXTRA_SETUP"
        print >>f, "#define EXTRA_SETUP() static int extra_setup(struct inst_data *ip, char *name, long extra_arg)"
        print >>f, "#undef EXTRA_CLEANUP"
        print >>f, "#define EXTRA_CLEANUP() static void extra_cleanup(void)"
        print >>f, "#undef fperiod"
        print >>f, "#define fperiod (period * 1e-9)"
        for name, type, array, dir, value in pins:
            print >>f, "#undef %s" % to_c(name)
            if array:
                if dir == 'in':
                    print >>f, "#define %s(i) (0+*(ip->%s[i]))" % (to_c(name), to_c(name))
                else:
                    print >>f, "#define %s(i) (*(ip->%s[i]))" % (to_c(name), to_c(name))
            else:
                if dir == 'in':
                    print >>f, "#define %s (0+*ip->%s)" % (to_c(name), to_c(name))
                else:
                    print >>f, "#define %s (*ip->%s)" % (to_c(name), to_c(name))
        for name, type, array, dir, value in params:
            print >>f, "#undef %s" % to_c(name)
            if array:
                print >>f, "#define %s(i) (ip->%s[i])" % (to_c(name), to_c(name))
            else:
                print >>f, "#define %s (ip->%s)" % (to_c(name), to_c(name))

        for type, name, array, value in variables:
            name = name.replace("*", "")
            print >>f, "#undef %s" % name
            print >>f, "#define %s (ip->%s)" % (name, name)

        if has_data:
            print >>f, "#undef data"
            print >>f, "#define data (*(%s*)(ip->_data))" % options['data']

        if options.get("userspace"):
            print >>f, "#undef FOR_ALL_INSTS"
            print >>f, "#define FOR_ALL_INSTS() for(ip = __comp_first_inst; ip; ip = ip->_next)"
    print >>f
    print >>f

#########################  Epilogue - FUNCTION(_) printed in direct from file ################

def epilogue(f):
    data = options.get('data')
    print >>f
    if data:
        print >>f, "static int __comp_get_data_size(void) { return sizeof(%s); }" % data
## no point in defining if it does nothing and is not used?
#    else:
#        print >>f, "static int __comp_get_data_size(void) { return 0; }"

INSTALL, COMPILE, PREPROCESS, DOCUMENT, INSTALLDOC, VIEWDOC, MODINC = range(7)
modename = ("install", "compile", "preprocess", "document", "installdoc", "viewdoc", "print-modinc")

modinc = None
def find_modinc():
    global modinc
    if modinc: return modinc
    d = os.path.abspath(os.path.dirname(os.path.dirname(sys.argv[0])))
    for e in ['src', 'etc/linuxcnc', '/etc/linuxcnc', 'share/linuxcnc']:
        e = os.path.join(d, e, 'Makefile.modinc')
        if os.path.exists(e):
            modinc = e
            return e
    raise SystemExit, "Unable to locate Makefile.modinc"

#   build userspace  #################################################################
#
#   NB. add patch that jepler rejected for extra link args

def build_usr(tempdir, filename, mode, origfilename):
    binname = os.path.basename(os.path.splitext(filename)[0])

    makefile = os.path.join(tempdir, "Makefile")
    f = open(makefile, "w")
    print >>f, "%s: %s" % (binname, filename)
    print >>f, "\t$(CC) $(EXTRA_CFLAGS) -URTAPI -U__MODULE__ -DULAPI -Os %s -o $@ $< -Wl,-rpath,$(LIBDIR) -L$(LIBDIR) -llinuxcnchal %s" % (
        options.get("extra_compile_args", ""),
        options.get("extra_link_args", ""))
    print >>f, "include %s" % find_modinc()
    f.close()
    result = os.system("cd %s && make -S %s" % (tempdir, binname))
    if result != 0:
        raise SystemExit, os.WEXITSTATUS(result) or 1
    output = os.path.join(tempdir, binname)
    if mode == INSTALL:
        shutil.copy(output, os.path.join(BASE, "bin", binname))
    elif mode == COMPILE:
        shutil.copy(output, os.path.join(os.path.dirname(origfilename),binname))

#   build_rt  #######################################################################
#


def build_rt(tempdir, filename, mode, origfilename):
    objname = os.path.basename(os.path.splitext(filename)[0] + ".o")
    makefile = os.path.join(tempdir, "Makefile")
    f = open(makefile, "w")
    print >>f, "obj-m += %s" % objname
    print >>f, "include %s" % find_modinc()
    print >>f, "EXTRA_CFLAGS += -I%s" % os.path.abspath(os.path.dirname(origfilename))
    print >>f, "EXTRA_CFLAGS += -I%s" % os.path.abspath('.')
    f.close()
    if mode == INSTALL:
        target = "modules install"
    else:
        target = "modules"
    result = os.system("cd %s && make -S %s" % (tempdir, target))
    if result != 0:
        raise SystemExit, os.WEXITSTATUS(result) or 1
    if mode == COMPILE:
        for extension in ".ko", ".so", ".o":
            kobjname = os.path.splitext(filename)[0] + extension
            if os.path.exists(kobjname):
                shutil.copy(kobjname, os.path.basename(kobjname))
                break
        else:
            raise SystemExit, "Unable to copy module from temporary directory"

######################  docs man pages etc  ###########################################

def finddoc(section=None, name=None):
    for item in docs:
        if ((section == None or section == item[0]) and
                (name == None or name == item[1])): return item
    return None

def finddocs(section=None, name=None):
    for item in docs:
        if ((section == None or section == item[0]) and
                (name == None or name == item[1])):
                    yield item

def to_hal_man_unnumbered(s):
    s = "%s.%s" % (comp_name, s)
    s = s.replace("_", "-")
    s = s.rstrip("-")
    s = s.rstrip(".")
    s = re.sub("#+", lambda m: "\\fI" + "M" * len(m.group(0)) + "\\fB", s)
    # s = s.replace("-", "\\-")
    return s


def to_hal_man(s):
    if options.get("singleton"):
        s = "%s.%s" % (comp_name, s)
    else:
        s = "%s.\\fIN\\fB.%s" % (comp_name, s)
    s = s.replace("_", "-")
    s = s.rstrip("-")
    s = s.rstrip(".")
    s = re.sub("#+", lambda m: "\\fI" + "M" * len(m.group(0)) + "\\fB", s)
    # s = s.replace("-", "\\-")
    return s

def document(filename, outfilename):
    if outfilename is None:
        outfilename = os.path.splitext(filename)[0] + ".9comp"

    a, b = parse(filename)
    f = open(outfilename, "w")

    print >>f, ".TH %s \"9\" \"%s\" \"Machinekit Documentation\" \"HAL Component\"" % (comp_name.upper(), time.strftime("%F"))
    print >>f, ".de TQ\n.br\n.ns\n.TP \\\\$1\n..\n"

    print >>f, ".SH NAME\n"
    doc = finddoc('component')
    if doc and doc[2]:
        if '\n' in doc[2]:
            firstline, rest = doc[2].split('\n', 1)
        else:
            firstline = doc[2]
            rest = ''
        print >>f, "%s \\- %s" % (doc[1], firstline)
    else:
        rest = ''
        print >>f, "%s" % doc[1]


    print >>f, ".SH SYNOPSIS"
    if options.get("userspace"):
        print >>f, ".B %s" % comp_name
    else:
        if rest:
            print >>f, rest
        else:
            print >>f, ".HP"
            if options.get("singleton") or options.get("count_function"):
                print >>f, ".B loadrt %s" % comp_name,
            else:
                print >>f, ".B loadrt %s [count=\\fIN\\fB|names=\\fIname1\\fB[,\\fIname2...\\fB]]" % comp_name,
            for type, name, default, doc in modparams:
                print >>f, "[%s=\\fIN\\fB]" % name,
            print >>f

            hasparamdoc = False
            for type, name, default, doc in modparams:
                if doc: hasparamdoc = True

            if hasparamdoc:
                print >>f, ".RS 4"
                for type, name, default, doc in modparams:
                    print >>f, ".TP"
                    print >>f, "\\fB%s\\fR" % name,
                    if default:
                        print >>f, "[default: %s]" % default
                    else:
                        print >>f
                    print >>f, doc
                print >>f, ".RE"

        if options.get("constructable") and not options.get("singleton"):
            print >>f, ".PP\n.B newinst %s \\fIname\\fB" % comp_name

    doc = finddoc('descr')
    if doc and doc[1]:
        print >>f, ".SH DESCRIPTION\n"
        print >>f, "%s" % doc[1]

    if functions:
        print >>f, ".SH FUNCTIONS"
        for _, name, fp, doc in finddocs('funct'):
            print >>f, ".TP"
            print >>f, "\\fB%s\\fR" % to_hal_man(name),
            if fp:
                print >>f, "(requires a floating-point thread)"
            else:
                print >>f
            print >>f, doc

    lead = ".TP"
    print >>f, ".SH PINS"
    for _, name, type, array, dir, doc, value in finddocs('pin'):
        print >>f, lead
        print >>f, ".B %s\\fR" % to_hal_man(name),
        print >>f, type, dir,
        if array:
            sz = name.count("#")
            if isinstance(array, tuple):
                print >>f, " (%s=%0*d..%s)" % ("M" * sz, sz, 0, array[1]),
            else:
                print >>f, " (%s=%0*d..%0*d)" % ("M" * sz, sz, 0, sz, array-1),
        if value:
            print >>f, "\\fR(default: \\fI%s\\fR)" % value
        else:
            print >>f, "\\fR"
        if doc:
            print >>f, doc
            lead = ".TP"
        else:
            lead = ".TQ"

    lead = ".TP"
    if params:
        print >>f, ".SH PARAMETERS"
        for _, name, type, array, dir, doc, value in finddocs('param'):
            print >>f, lead
            print >>f, ".B %s\\fR" % to_hal_man(name),
            print >>f, type, dir,
            if array:
                sz = name.count("#")
                if isinstance(array, tuple):
                    print >>f, " (%s=%0*d..%s)" % ("M" * sz, sz, 0, array[1]),
                else:
                    print >>f, " (%s=%0*d..%0*d)" % ("M" * sz, sz, 0, sz, array-1),
            if value:
                print >>f, "\\fR(default: \\fI%s\\fR)" % value
            else:
                print >>f, "\\fR"
            if doc:
                print >>f, doc
                lead = ".TP"
            else:
                lead = ".TQ"


    if instanceparams:
        print >>f, ".SH INST_PARAMETERS"
        for _, name, type, doc, value in finddocs('instanceparam'):
            print >>f, lead
            print >>f, ".B %s\\fR" % to_hal_man(name),
            print >>f, type,
#            if array:
#                sz = name.count("#")
#                if isinstance(array, tuple):
#                    print >>f, " (%s=%0*d..%s)" % ("M" * sz, sz, 0, array[1]),
#                else:
#                    print >>f, " (%s=%0*d..%0*d)" % ("M" * sz, sz, 0, sz, array-1),
            if value:
                print >>f, "\\fR(default: \\fI%s\\fR)" % value
            else:
                print >>f, "\\fR"
            if doc:
                print >>f, doc
                lead = ".TP"
            else:
                lead = ".TQ"

    if moduleparams:
        print >>f, ".SH MODULE_PARAMETERS"
        for _, name, type, doc, value in finddocs('moduleparam'):
            print >>f, lead
            print >>f, ".B %s\\fR" % to_hal_man(name),
            print >>f, type,
#            if array:
#                sz = name.count("#")
#                if isinstance(array, tuple):
#                    print >>f, " (%s=%0*d..%s)" % ("M" * sz, sz, 0, array[1]),
#                else:
#                    print >>f, " (%s=%0*d..%0*d)" % ("M" * sz, sz, 0, sz, array-1),
            if value:
                print >>f, "\\fR(default: \\fI%s\\fR)" % value
            else:
                print >>f, "\\fR"
            if doc:
                print >>f, doc
                lead = ".TP"
            else:
                lead = ".TQ"

    doc = finddoc('see_also')
    if doc and doc[1]:
        print >>f, ".SH SEE ALSO\n"
        print >>f, "%s" % doc[1]

    doc = finddoc('notes')
    if doc and doc[1]:
        print >>f, ".SH NOTES\n"
        print >>f, "%s" % doc[1]

    doc = finddoc('author')
    if doc and doc[1]:
        print >>f, ".SH AUTHOR\n"
        print >>f, "%s" % doc[1]

    doc = finddoc('license')
    if doc and doc[1]:
        print >>f, ".SH LICENSE\n"
        print >>f, "%s" % doc[1]

def process(filename, mode, outfilename):
    tempdir = tempfile.mkdtemp()
    try:
        if outfilename is None:
            if mode == PREPROCESS:
                outfilename = os.path.splitext(filename)[0] + ".c"
            else:
                outfilename = os.path.join(tempdir,
                    os.path.splitext(os.path.basename(filename))[0] + ".c")

        a, b = parse(filename)
        f = open(outfilename, "w")

        if options.get("userinit") and not options.get("userspace"):
            print >> sys.stderr, "Warning: comp '%s' sets 'userinit' without 'userspace', ignoring" % filename

        if options.get("userspace"):
            if functions:
                raise SystemExit, "Userspace components may not have functions"
        if not pins:
            raise SystemExit, "Component must have at least one pin"
        prologue(f)
        lineno = a.count("\n") + 3

        if options.get("userspace"):
            if functions:
                raise SystemExit, "May not specify functions with a userspace component."
            f.write("#line %d \"%s\"\n" % (lineno, filename))
            f.write(b)
        else:
            if not functions or "FUNCTION" in b:
                f.write("#line %d \"%s\"\n" % (lineno, filename))
                f.write(b)
            elif len(functions) == 1:
                f.write("FUNCTION(%s) {\n" % functions[0][0])
                f.write("#line %d \"%s\"\n" % (lineno, filename))
                f.write(b)
                f.write("}\n")
            else:
                raise SystemExit, "Must use FUNCTION() when more than one function is defined"
        epilogue(f)
        f.close()

        if mode != PREPROCESS:
            if options.get("userspace"):
                build_usr(tempdir, outfilename, mode, filename)
            else:
                build_rt(tempdir, outfilename, mode, filename)

    finally:
        shutil.rmtree(tempdir)

def usage(exitval=0):
    print """%(name)s: Build, compile, and install LinuxCNC HAL components

Usage:
           %(name)s [--compile|--preprocess|--document|--view-doc] compfile...
    [sudo] %(name)s [--install|--install-doc] compfile...
           %(name)s --compile --userspace cfile...
    [sudo] %(name)s --install --userspace cfile...
    [sudo] %(name)s --install --userspace pyfile...
           %(name)s --print-modinc
""" % {'name': os.path.basename(sys.argv[0])}
    raise SystemExit, exitval

#####################################  main  ##################################

def main():
    global require_license
    require_license = True
    mode = PREPROCESS
    outfile = None
    userspace = False
    try:
        opts, args = getopt.getopt(sys.argv[1:], "luijcpdo:h?",
                           ['install', 'compile', 'preprocess', 'outfile=',
                            'document', 'help', 'userspace', 'install-doc',
                            'view-doc', 'require-license', 'print-modinc'])
    except getopt.GetoptError:
        usage(1)

    for k, v in opts:
        if k in ("-u", "--userspace"):
            userspace = True
        if k in ("-i", "--install"):
            mode = INSTALL
        if k in ("-c", "--compile"):
            mode = COMPILE
        if k in ("-p", "--preprocess"):
            mode = PREPROCESS
        if k in ("-d", "--document"):
            mode = DOCUMENT
        if k in ("-j", "--install-doc"):
            mode = INSTALLDOC
        if k in ("-j", "--view-doc"):
            mode = VIEWDOC
        if k in ("--print-modinc",):
            mode = MODINC
        if k in ("-l", "--require-license"):
            require_license = True
        if k in ("-o", "--outfile"):
            if len(args) != 1:
                raise SystemExit, "Cannot specify -o with multiple input files"
            outfile = v
        if k in ("-?", "-h", "--help"):
            usage(0)

    if outfile and mode != PREPROCESS and mode != DOCUMENT:
        raise SystemExit, "Can only specify -o when preprocessing or documenting"

    if mode == MODINC:
        if args:
            raise SystemExit, \
                "Can not specify input files when using --print-modinc"
        print find_modinc()
        return 0

    for f in args:
        try:
            basename = os.path.basename(os.path.splitext(f)[0])
            if f.endswith(".comp") and mode == DOCUMENT:
                document(f, outfile)
            elif f.endswith(".comp") and mode == VIEWDOC:
                tempdir = tempfile.mkdtemp()
                try:
                    outfile = os.path.join(tempdir, basename + ".9comp")
                    document(f, outfile)
                    os.spawnvp(os.P_WAIT, "man", ["man", outfile])
                finally:
                    shutil.rmtree(tempdir)
            elif f.endswith(".comp") and mode == INSTALLDOC:
                manpath = os.path.join(BASE, "share/man/man9")
                if not os.path.isdir(manpath):
                    manpath = os.path.join(BASE, "man/man9")
                outfile = os.path.join(manpath, basename + ".9comp")
                print "INSTALLDOC", outfile
                document(f, outfile)
            elif f.endswith(".comp"):
                process(f, mode, outfile)
            elif f.endswith(".py") and mode == INSTALL:
                lines = open(f).readlines()
                if lines[0].startswith("#!"): del lines[0]
                lines[0] = "#!%s\n" % sys.executable
                outfile = os.path.join(BASE, "bin", basename)
                try: os.unlink(outfile)
                except os.error: pass
                open(outfile, "w").writelines(lines)
                os.chmod(outfile, 0555)
            elif f.endswith(".c") and mode != PREPROCESS:
                initialize()
                tempdir = tempfile.mkdtemp()
                try:
                    shutil.copy(f, tempdir)
                    if userspace:
                        build_usr(tempdir, os.path.join(tempdir, os.path.basename(f)), mode, f)
                    else:
                        build_rt(tempdir, os.path.join(tempdir, os.path.basename(f)), mode, f)
                finally:
                    shutil.rmtree(tempdir)
            else:
                raise SystemExit, "Unrecognized file type for mode %s: %r" % (modename[mode], f)
        except:
            ex_type, ex_value, exc_tb = sys.exc_info()
            try:
                os.unlink(outfile)
            except: # os.error:
                pass
            raise ex_type, ex_value, exc_tb
if __name__ == '__main__':
    main()

# vim:sw=4:sts=4:et
