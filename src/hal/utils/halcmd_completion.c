/* Copyright (C) 2007 Jeff Epler <jepler@unpythonic.net>
 * Copyright (C) 2003 John Kasunich
 *                     <jmkasunich AT users DOT sourceforge DOT net>
 *
 *  Other contributers:
 *                     Martin Kuhnle
 *                     <mkuhnle AT users DOT sourceforge DOT net>
 *                     Alex Joni
 *                     <alex_joni AT users DOT sourceforge DOT net>
 *                     Benn Lipkowitz
 *                     <fenn AT users DOT sourceforge DOT net>
 *                     Stephen Wille Padnos
 *                     <swpadnos AT users DOT sourceforge DOT net>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of version 2 of the GNU General
 *  Public License as published by the Free Software Foundation.
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111 USA
 *
 *  THE AUTHORS OF THIS LIBRARY ACCEPT ABSOLUTELY NO LIABILITY FOR
 *  ANY HARM OR LOSS RESULTING FROM ITS USE.  IT IS _EXTREMELY_ UNWISE
 *  TO RELY ON SOFTWARE ALONE FOR SAFETY.  Any machinery capable of
 *  harming persons must have provisions for completely removing power
 *  from all motors, etc, before persons enter any danger area.  All
 *  machinery must be designed to comply with local and national safety
 *  codes, and the authors of this software can not, and do not, take
 *  any responsibility for such compliance.
 *
 *  This code was written as part of the EMC HAL project.  For more
 *  information, go to www.linuxcnc.org.
 */

#include "halcmd_completion.h"
#include "config.h"
#include "rtapi.h"		/* RTAPI realtime OS API */
#include "hal.h"		/* HAL public API decls */
#include "hal_priv.h"	/* private HAL decls */
#include "hal_ring.h"	        /* ringbuffer declarations */
#include "hal_group.h"	        /* halgroups declarations */

static int argno;

static const char *command_table[] = {
    "loadrt", "loadusr", "unload", "lock", "unlock",
    "linkps", "linksp", "linkpp", "unlinkp",
    "net", "newsig", "delsig", "getp", "gets", "setp", "sets", "sete", "ptype", "stype",
    "addf", "delf", "show", "list", "status", "save", "source",
    "start", "stop", "quit", "exit", "help", "alias", "unalias", 
    "newg"," delg", "newm", "delm",
    "newring","delring","ringdump","ringwrite","ringread",
    "newcomp","newpin","ready","waitbound", "waitunbound", "waitexists",
    "log","shutdown","ping","newthread","delthread",
    "sleep","vtable","autoload","newinst", "delinst",
    NULL,
};

static const char *nonRT_command_table[] = {
    "-h",
    NULL,
};

static const char *alias_table[] = {
    "param", "pin",
    NULL,
};

static const char *show_table[] = {
    "all", "alias", "comp", "pin", "sig", "param", "funct", "thread", "group", "member",
    "ring", "eps","vtable","inst",
    NULL,
};

static const char *save_table[] = {
    "all", "alias", "comp", "sig", "link", "linka", "net", "neta", "param", "thread",
    "group", "member", "ring",
    NULL,
};

static const char *list_table[] = {
    "comp", "alias", "pin", "sig", "param", "funct", "thread", "group", "member",
    "ring","inst",
    NULL
};

static const char *status_table[] = {
    "alias", "lock", "mem", "all",
    NULL
};

static const char *pintype_table[] = {
    "bit", "float", "u32", "s32", 
    NULL
};

static const char *log_table[] = {
    "rt", "user",
    NULL
};

static const char *lock_table[] = { "none", "tune", "all", NULL };
static const char *unlock_table[] = { "tune", "all", NULL };

static const char **string_table = NULL;

static char *table_generator(const char *text, int state) {
    static int len;
    static int list_index = 0;
    const char *name;

    if(state == 0) {
        list_index = 0;
        len = strlen(text);
    }

    while((name = string_table[list_index]) != NULL) {
        list_index ++;
        if(strncmp (name, text, len) == 0) return strdup(name);
    }
    return NULL;
}

static char **completion_matches_table(const char *text, const char **table, hal_completer_func func) {
    string_table = table;
    return func(text, table_generator);
}

static hal_type_t match_type = -1;
static int match_writers = -1;
static hal_pin_dir_t match_direction = HAL_DIR_UNSPECIFIED;

static int direction_match(hal_pin_dir_t dir1, hal_pin_dir_t dir2) {
    if(dir1 == HAL_DIR_UNSPECIFIED || dir2 == -1) return 1;
    return (dir1 | dir2) == HAL_IO;
}

static int writer_match(hal_pin_dir_t dir, int writers) {
    if(writers == -1 || dir == -1) return 1;
    if(dir & HAL_IN || writers == 0) return 1;
    return 0;
}

static void check_match_type_pin(const char *name) {
#if 0
    int next = hal_data->pin_list_ptr;
    int sz = strcspn(name, " \t");

    while(next) {
        hal_pin_t *pin = SHMPTR(next);
        next = pin->next_ptr;
	if ( sz == strlen(pin->name) && strncmp(name, pin->name, sz) == 0 ) {
            match_type = pin->type;
            match_direction = pin->dir;
            return;
        }
    }
#endif
}

static void check_match_type_signal(const char *name) {
#warning FIXME
#if 0
    int next = hal_data->sig_list_ptr;
    int sz = strcspn(name, " \t");
    while(next) {
        hal_sig_t *sig = SHMPTR(next);
        next = sig->next_ptr;
	if ( sz == strlen(sig->name) && strncmp(name, sig->name, sz) == 0 ) {
            match_type = sig->type;
            match_writers = sig->writers;
            return;
        }
    }
#endif
}

static char *thread_generator(const char *text, int state) { 
    static int len;
    static int next;
    if(!state) {
        next = hal_data->thread_list_ptr;
        len = strlen(text);
    }

    while(next) {
        hal_thread_t *thread = SHMPTR(next);
        next = thread->next_ptr;
	if ( strncmp(text, thread->name, len) == 0 )
            return strdup(thread->name);
    }
    return NULL;
}

static char *parameter_generator(const char *text, int state) { 
    static int len;
    static int next;
    static int aliased;
    char *name;

#warning FIXME
#if 0
    if(!state) {
        next = hal_data->param_list_ptr;
        len = strlen(text);
        aliased = 0;
    }

    while(next) {
        hal_param_t *param = SHMPTR(next);
        switch (aliased) {
            case 0: // alias (if any) has not been output
                if (param->oldname != 0) {
                    // there's an alias, so use that and do not update the pin pointer
                    hal_oldname_t *oldname = SHMPTR(param->oldname);
                    name = oldname->name;
                    aliased = 1;
                } else {
                    // no alias, so use the name and update the pin pointer
                    name = param->name;
                    next = param->next_ptr;
                }
            break;
            case 1:  // there is an alias, and it has been processed already
                name = param->name;
                next = param->next_ptr;
                aliased = 0;
            break;
            default:
                // shouldn't be able to get here, so assume we're done
                rl_attempted_completion_over = 1;
                return NULL;
            break;
        }
        if ( strncmp(text, name, len) == 0 )
            return strdup(name);
    }
#endif
    return NULL;
}

static char *funct_generator_common(const char *text, int state, int inuse) { 
    static int len;
    static int next;

#warning fix this
#if 0
    if(!state) {
        next = hal_data->funct_list_ptr;
        len = strlen(text);
    }

    while(next) {
        hal_funct_t *funct = SHMPTR(next);
        next = funct->next_ptr;
	if (( strncmp(text, funct->name, len) == 0 ) 
            && (inuse == funct->users))
            return strdup(funct->name);
    }
#endif
    return NULL;
}

static char *funct_generator(const char *text, int state) {
    return funct_generator_common(text, state, 0);
}

static char *attached_funct_generator(const char *text, int state) {
    return funct_generator_common(text, state, 1);
}

static char *signal_generator(const char *text, int state) {
#if 0
    static int len;
    static int next;

    if(!state) {
        next = hal_data->sig_list_ptr;
        len = strlen(text);
    }

    while(next) {
        hal_sig_t *sig = SHMPTR(next);
        next = sig->next_ptr;
        if ( match_type != HAL_TYPE_UNSPECIFIED && match_type != sig->type ) continue; 
        if ( !writer_match( match_direction, sig->writers ) ) continue;
	if ( strncmp(text, sig->name, len) == 0 )
            return strdup(sig->name);
    }
#endif
    return NULL;
}

static char *getp_generator(const char *text, int state) {
    static int len;
    static int next;
    static int what;

#warning FIXME
#if 0
    if(!state) {
        what = 0;
        next = hal_data->param_list_ptr;
        len = strlen(text);
    }

    if(what == 0) {
        while(next) {
            hal_param_t *param = SHMPTR(next);
            next = param->next_ptr;
            if ( strncmp(text, param->name, len) == 0 )
                return strdup(param->name);
        }
        what = 1;
        next = hal_data->pin_list_ptr;
    }
    while(next) {
        hal_pin_t *pin = SHMPTR(next);
        next = pin->next_ptr;
        if ( strncmp(text, pin->name, len) == 0 )
            return strdup(pin->name);
    }
#endif
    return NULL;
}

static char *setp_generator(const char *text, int state) {
    static int len;
    static int next;
    static int what;
#warning FIXME
#if 0

    if(!state) {
        what = 0;
        next = hal_data->param_list_ptr;
        len = strlen(text);
    }

    if(what == 0) {
        while(next) {
            hal_param_t *param = SHMPTR(next);
            next = param->next_ptr;
            if ( param->dir != HAL_RO && strncmp(text, param->name, len) == 0 )
                return strdup(param->name);
        }
        what = 1;
        next = hal_data->pin_list_ptr;
    }
    while(next) {
        hal_pin_t *pin = SHMPTR(next);
        next = pin->next_ptr;
        if ( pin->dir != HAL_OUT && pin->signal == 0 && 
                 strncmp(text, pin->name, len) == 0 )
            return strdup(pin->name);
    }
#endif
    return NULL;
}


static char *usrcomp_generator(const char *text, int state) {
    static int len;
    static int next;
    if(!state) {
        next = hal_data->comp_list_ptr;
        len = strlen(text);
        if(strncmp(text, "all", len) == 0)
            return strdup("all");
    }

    while(next) {
        hal_comp_t *comp = SHMPTR(next);
        next = comp->next_ptr;
        if(comp->type == TYPE_RT) continue;
	if(strncmp(text, comp->name, len) == 0)
            return strdup(comp->name);
    }
    rl_attempted_completion_over = 1;
    return NULL;
}


// return names of loaded comps which are instantiable
static char *icomp_generator(const char *text, int state) {
    static int len;
    static int next;
    if(!state) {
        next = hal_data->comp_list_ptr;
        len = strlen(text);
    }

    while(next) {
        hal_comp_t *comp = SHMPTR(next);
        next = comp->next_ptr;
        if(comp->type != TYPE_RT) continue;
	if (comp->ctor == NULL) continue;
	if(strncmp(text, comp->name, len) == 0)
            return strdup(comp->name);
    }
    rl_attempted_completion_over = 1;
    return NULL;
}


static char *comp_generator(const char *text, int state) {
    static int len;
    static int next;
    if(!state) {
        next = hal_data->comp_list_ptr;
        len = strlen(text);
        if(strncmp(text, "all", len) == 0)
            return strdup("all");
    }

    while(next) {
        hal_comp_t *comp = SHMPTR(next);
        next = comp->next_ptr;
	if ( strncmp(text, comp->name, len) == 0 )
            return strdup(comp->name);
    }
    rl_attempted_completion_over = 1;
    return NULL;
}


static char *rtcomp_generator(const char *text, int state) {
    static int len;
    static int next;
    if(!state) {
        next = hal_data->comp_list_ptr;
        len = strlen(text);
        if(strncmp(text, "all", len) == 0)
            return strdup("all");
    }

    while(next) {
        hal_comp_t *comp = SHMPTR(next);
        next = comp->next_ptr;
        if(comp->type != TYPE_RT) continue;
	if ( strncmp(text, comp->name, len) == 0 )
            return strdup(comp->name);
    }
    rl_attempted_completion_over = 1;
    return NULL;
}

static char *parameter_alias_generator(const char *text, int state) {
    static int len;
    static int next;

#warning FIXME
#if 0
    if(!state) {
        next = hal_data->param_list_ptr;
        len = strlen(text);
    }

    while(next) {
        hal_param_t *param = SHMPTR(next);
        next = param->next_ptr;
        if (param->oldname==0) continue;  // no alias here, move along
        if ( strncmp(text, param->name, len) == 0 )
            return strdup(param->name);
    }
#endif
    return NULL;
}

static char *pin_alias_generator(const char *text, int state) {
    static int len;
    static int next;

    if(!state) {
        next = hal_data->pin_list_ptr;
        len = strlen(text);
    }
#if 0
    while(next) {
        hal_pin_t *pin = SHMPTR(next);
        next = pin->next_ptr;
	//        if (pin->oldname==0) continue;  // no alias here, move along
        if ( strncmp(text, pin->name, len) == 0 )
            return strdup(pin->name);
    }
#endif
    return NULL;
}

static char *pin_generator(const char *text, int state) {
    static int len;
    static int next;
    static int aliased;
    char *name;
#if 0		
    if(!state) {
        next = hal_data->pin_list_ptr;
        len = strlen(text);
        aliased = 0;
    }

    while(next) {
        hal_pin_t *pin = SHMPTR(next);
        switch (aliased) {
            case 0: // alias (if any) has not been output
#if 0		
                if (pin->oldname != 0) {
                    // there's an alias, so use that and do not update the pin pointer
                    hal_oldname_t *oldname = SHMPTR(pin->oldname);
                    name = oldname->name;
                    aliased = 1;
                } else {
#endif
                    // no alias, so use the name and update the pin pointer
                    name = pin->name;
                    next = pin->next_ptr;
   //          }
            break;
            case 1:  // there is an alias, and it has been processed already
                name = pin->name;
                next = pin->next_ptr;
                aliased = 0;
            break;
            default:
                // shouldn't be able to get here, so assume we're done
                rl_attempted_completion_over = 1;
                return NULL;
            break;
        }
        if ( !writer_match( pin->dir, match_writers ) ) continue;
        if ( !direction_match( pin->dir, match_direction ) ) continue;
        if ( match_type != HAL_TYPE_UNSPECIFIED && match_type != pin->type ) continue; 
	if ( strncmp(text, name, len) == 0 )
            return strdup(name);
    }
    rl_attempted_completion_over = 1;
#endif

    return NULL;
}

#include <dirent.h>

static int startswith(const char *string, const char *stem) {
    return strncmp(string, stem, strlen(stem)) == 0;
}

const char *loadusr_table[] = {"-W", "-Wn", "-w", "-iw", NULL};

static char *loadusr_generator(const char *text, int state) {
    static int len;
    static DIR *d;
    struct dirent *ent;
    static int doing_table;
    char bindir[PATH_MAX];

    if (get_rtapi_config(bindir,"BIN_DIR",PATH_MAX) != 0)
	return NULL;

    if(!state) {
	if(argno == 1) doing_table = 1;
        string_table = loadusr_table;
        len = strlen(text);
        d = opendir(bindir);
    }

    if(doing_table) {
    	char *result = table_generator(text, state);
        if(result) return result;
        doing_table = 0;
    }

    while(d && (ent = readdir(d))) {
        char *result;
        if(!startswith(ent->d_name, "hal")) continue;
        if(startswith(ent->d_name, "halcmd")) continue;
        if(strncmp(text, ent->d_name, len) != 0) continue;
        result = strdup(ent->d_name);
        return result;
    }
    if (d != NULL) {
        closedir(d);
    }
    return NULL;
}

extern flavor_ptr current_flavor; // reference to current flavor descriptor

static char *loadrt_generator(const char *text, int state) {
    static int len;
    static DIR *d;
    struct dirent *ent;
    char rtlibdir[PATH_MAX];

    if (get_rtapi_config(rtlibdir,"RTLIB_DIR",PATH_MAX) != 0)
	return NULL;

    strcat(rtlibdir,"/");
    strcat(rtlibdir, current_flavor->name);
    strcat(rtlibdir,"/");

    if(!state) {
        len = strlen(text);
        d = opendir(rtlibdir);
    }

    while(d && (ent = readdir(d))) {
        char *result;
        if(!strstr(ent->d_name, default_flavor()->mod_ext)) continue;
        if(startswith(ent->d_name, "rtapi.")) continue;
        if(startswith(ent->d_name, "hal_lib.")) continue;
        if(strncmp(text, ent->d_name, len) != 0) continue;
        result = strdup(ent->d_name);
        result[strlen(result) - \
	       strlen(default_flavor()->mod_ext)] = 0;
        return result;
    }
    if (d != NULL) {
        closedir(d);
    }
    return NULL;
}

static char *group_generator(const char *text, int state) {
    static int len;
    static int next;

    if(!state) {
        next = hal_data->group_list_ptr;
        len = strlen(text);
    }

    while(next) {
        hal_group_t *group = SHMPTR(next);
        next = group->next_ptr;
        if ( strncmp(text, group->name, len) == 0 )
            return strdup(group->name);
    }
    return NULL;
}

static char *ring_generator(const char *text, int state) {
    static int len;
    static int next;

    if(!state) {
        next = hal_data->ring_list_ptr;
        len = strlen(text);
    }

    while(next) {
        hal_ring_t *ring = SHMPTR(next);
        next = ring->next_ptr;
        if ( strncmp(text, ring->name, len) == 0 )
            return strdup(ring->name);
    }
    return NULL;
}

static char *inst_generator(const char *text, int state) {
    static int len;
    static int next;
#warning FIXME
#if 0
    if(!state) {
        next = hal_data->inst_list_ptr;
        len = strlen(text);
    }

    while(next) {
        hal_inst_t *inst = SHMPTR(next);
        next = inst->next_ptr;
        if ( strncmp(text, inst->name, len) == 0 )
            return strdup(inst->name);
    }
#endif
    return NULL;
}


static inline int isskip(int ch) {
    return isspace(ch) || ch == '=' || ch == '<' || ch == '>';
}

static char *nextword(char *s) {
    s = strchr(s, ' ');
    if(!s) return NULL;
    return s+strspn(s, " \t=<>");
}

char **halcmd_completer(const char *text, int start, int end, hal_completer_func func, char *buffer) {
    int i;
    char **result = NULL, *n;

    while (isskip(*text)) text++;   // skip initial whitespace
    while (isskip(*buffer)) {
        buffer++;
        start--;
    }
    if (start<0) start=0;
    if(start == 0) {
        if (comp_id >= 0) {
            return completion_matches_table(text, command_table, func);
        } else {
            return completion_matches_table(text, nonRT_command_table, func);
        }
    }

    for(i=0, argno=0; i<start; i++) {
        if(isskip(buffer[i])) {
            argno++;
            while(i<start && isskip(buffer[i])) i++;
        }
    }

    match_type = -1;
    match_writers = -1;
    match_direction = -1;

    rtapi_mutex_get(&(hal_data->mutex));

    if(startswith(buffer, "delsig ") && argno == 1) {
        result = func(text, signal_generator);
    } else if(startswith(buffer, "delg ") && argno == 1) {
        result = func(text, group_generator);
    } else if(startswith(buffer, "delinst ") && argno == 1) {
        result = func(text, inst_generator);
    } else if(startswith(buffer, "delm ") && argno == 1) {
        result = func(text, group_generator);
    } else if(startswith(buffer, "delt ") && argno == 1) {
        result = func(text, thread_generator);

    } else if(startswith(buffer, "newr ") && argno > 2) {
	result = func(text, ring_generator);
    } else if(startswith(buffer, "delr ") && argno == 1) {
        result = func(text, ring_generator);
    } else if(startswith(buffer, "ringdump ") && argno == 1) {
	result = func(text, ring_generator);
   } else if(startswith(buffer, "ringread ") && argno == 1) {
	result = func(text, ring_generator);
   } else if(startswith(buffer, "ringwrite ") && argno == 1) {
	result = func(text, ring_generator);
    } else if(startswith(buffer, "linkps ") && argno == 1) {
        result = func(text, pin_generator);
    } else if(startswith(buffer, "linkps ") && argno == 2) {
        check_match_type_pin(buffer + 7);
        result = func(text, signal_generator);
    } else if(startswith(buffer, "net ") && argno == 1) {
        result = func(text, signal_generator);
    } else if(startswith(buffer, "net ") && argno == 2) {
        check_match_type_signal(nextword(buffer));
        result = func(text, pin_generator);


    } else if(startswith(buffer, "net ") && argno > 2) {
        check_match_type_signal(nextword(buffer));
        if(match_type == HAL_TYPE_UNSPECIFIED) {
            check_match_type_pin(nextword(nextword(buffer)));
            if(match_direction == HAL_IN) match_direction = -1;
        }
        result = func(text, pin_generator);
    } else if(startswith(buffer, "linksp ") && argno == 1) {
        result = func(text, signal_generator);
    } else if(startswith(buffer, "linksp ") && argno == 2) {
        check_match_type_signal(buffer + 7);
        result = func(text, pin_generator);
    } else if (startswith(buffer, "alias ")) {
        if (argno == 1) {
            result = completion_matches_table(text, alias_table, func);
        } else if (argno == 2) {
            n = nextword(buffer);
            if (startswith(n, "pin")) {
                result = func(text, pin_generator);
            } else if (startswith(n, "param")) {
                result = func(text, parameter_generator);
            }
        }
    } else if(startswith(buffer, "unalias ")) {
        if (argno == 1) {
            result = completion_matches_table(text, alias_table, func);
        } else if (argno == 2) {
            n = nextword(buffer);
            if (startswith(n, "pin")) {
                result = func(text, pin_alias_generator);
            } else if (startswith(n, "param")) {
                result = func(text, parameter_alias_generator);
            }
        }
    } else if(startswith(buffer, "linkpp ") && argno == 1) {
        result = func(text, pin_generator);
    } else if(startswith(buffer, "linkpp ") && argno == 2) {
        check_match_type_pin(buffer + 7);
        result = func(text, pin_generator);
    } else if(startswith(buffer, "unlinkp ") && argno == 1) {
        result = func(text, pin_generator);
    } else if(startswith(buffer, "setp ") && argno == 1) {
        result = func(text, setp_generator);
    } else if(startswith(buffer, "sets ") && argno == 1) {
        result = func(text, signal_generator);
    } else if(startswith(buffer, "ptype ") && argno == 1) {
        result = func(text, getp_generator);
    } else if(startswith(buffer, "getp ") && argno == 1) {
        result = func(text, getp_generator);
    } else if(startswith(buffer, "stype ") && argno == 1) {
        result = func(text, signal_generator);
    } else if(startswith(buffer, "gets ") && argno == 1) {
        result = func(text, signal_generator);
    } else if(startswith(buffer, "list ")) {
        if (argno == 1) {
            result = completion_matches_table(text, list_table, func);
        } else if (argno==2) {
            n = nextword(buffer);
            if (startswith(n, "pin")) {
                result = func(text, pin_generator);
            } else if (startswith(n, "sig")) {
                result = func(text, signal_generator);
            } else if (startswith(n, "param")) {
                result = func(text, parameter_generator);
            } else if (startswith(n, "funct")) {
                result = func(text, funct_generator);
            } else if (startswith(n, "thread")) {
                result = func(text, thread_generator);
	    } else if (startswith(n, "group")) {
                result = func(text, group_generator);
	    } else if (startswith(n, "inst")) {
                result = func(text, inst_generator);
	    } else if (startswith(n, "ring")) {
                result = func(text, ring_generator);
            }
        }
    } else if(startswith(buffer, "show ")) {
        if (argno == 1) {
            result = completion_matches_table(text, show_table, func);
        } else if (argno==2) {
            n = nextword(buffer);
            if (startswith(n, "pin")) {
                result = func(text, pin_generator);
            } else if (startswith(n, "sig")) {
                result = func(text, signal_generator);
            } else if (startswith(n, "param")) {
                result = func(text, parameter_generator);
            } else if (startswith(n, "funct")) {
                result = func(text, funct_generator);
            } else if (startswith(n, "thread")) {
                result = func(text, thread_generator);
            } else if (startswith(n, "group")) {
                result = func(text, group_generator);
	    } else if (startswith(n, "ring")) {
                result = func(text, ring_generator);
            }
        }
    } else if(startswith(buffer, "save ") && argno == 1) {
        result = completion_matches_table(text, save_table, func);
    } else if(startswith(buffer, "status ") && argno == 1) {
        result = completion_matches_table(text, status_table, func);
    } else if(startswith(buffer, "newsig ") && argno == 2) {
        result = completion_matches_table(text, pintype_table, func);
    } else if(startswith(buffer, "lock ") && argno == 1) {
        result = completion_matches_table(text, lock_table, func);
    } else if(startswith(buffer, "log ") && argno == 1) {
        result = completion_matches_table(text, log_table, func);
    } else if(startswith(buffer, "unlock ") && argno == 1) {
        result = completion_matches_table(text, unlock_table, func);
    } else if(startswith(buffer, "addf ") && argno == 1) {
        result = func(text, funct_generator);
    } else if(startswith(buffer, "call ") && argno == 1) {
        result = func(text, funct_generator);
    } else if(startswith(buffer, "addf ") && argno == 2) {
        result = func(text, thread_generator);
    } else if(startswith(buffer, "delf ") && argno == 1) {
        result = func(text, attached_funct_generator);
    } else if(startswith(buffer, "delf ") && argno == 2) {
        result = func(text, thread_generator);
    } else if(startswith(buffer, "help ") && argno == 1) {
        result = completion_matches_table(text, command_table, func);
    } else if(startswith(buffer, "unloadusr ") && argno == 1) {
        result = func(text, usrcomp_generator);
    } else if(startswith(buffer, "newinst ") && argno == 1) {
        result = func(text, icomp_generator);
    } else if(startswith(buffer, "waitusr ") && argno == 1) {
        result = func(text, usrcomp_generator);
    } else if(startswith(buffer, "unloadrt ") && argno == 1) {
        result = func(text, rtcomp_generator);
    } else if(startswith(buffer, "unload ") && argno == 1) {
        result = func(text, comp_generator);
    } else if(startswith(buffer, "source ") && argno == 1) {
        rtapi_mutex_give(&(hal_data->mutex));
        // leaves rl_attempted_completion_over = 0 to complete from filesystem
        return 0;
    } else if(startswith(buffer, "loadusr ") && argno < 3) {
        rtapi_mutex_give(&(hal_data->mutex));
        // leaves rl_attempted_completion_over = 0 to complete from filesystem
        return func(text, loadusr_generator);
    } else if(startswith(buffer, "loadrt ") && argno == 1) {
        result = func(text, loadrt_generator);
    } else if(startswith(buffer, "delg ") && argno == 1) {
        result = func(text, group_generator);
    } else if(startswith(buffer, "delm ") && argno == 1) {
        result = func(text, group_generator);
    } else if(startswith(buffer, "newm ") && argno == 1) {
        result = func(text, group_generator);
    } else if(startswith(buffer, "newm ") && argno == 2) {
        result = func(text, signal_generator); // FIXME should be signal_and_pin_generator
    }

    rtapi_mutex_give(&(hal_data->mutex));

    rl_attempted_completion_over = 1;
    return result;
}

static char **rlcompleter(const char *text, int start, int end) {
    return halcmd_completer(text, start, end, rl_completion_matches, rl_line_buffer);
}

void halcmd_init_readline() {
    rl_readline_name = "halcmd";
    rl_attempted_completion_function = rlcompleter;
}

