

// return number of pins in a component
int halpr_pin_count(const char *name);


// return number of params in a component
int halpr_param_count(const char *name);

// hal mutex scope-locked version of halpr_find_pin_by_name()
hal_pin_t *
hal_find_pin_by_name(const char *name);

// return the state of a component, or -ENOENT on failure (e.g not existent)
int
hal_comp_state_by_name(const char *name);
