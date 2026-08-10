#pragma once
#include "dewlock.h"

// Parses argv into `state` and/or `config_path`. Either output pointer may be
// NULL to skip populating it, which lets callers run a first pass that only
// looks for --config before the config file is loaded, followed by a second
// pass that applies the remaining CLI flags on top of it.
// Returns 0 on success, or a value the caller should return from main.
int parse_cli_args(int argc, char **argv, struct dewlock_state *state,
                    char **config_path);

// Resolves the config file path from the well-known locations
// ($HOME/.dewlock/config, $XDG_CONFIG_HOME/dewlock/config, SYSCONFDIR).
// Returns NULL if none of them exist. Caller owns the returned string.
char *get_config_path(void);

// Parses the config file at `path` into `state->args`.
int load_config(char *path, struct dewlock_state *state);

// Loads the background image(s) referenced by state->args.background.path
// into state->images.
void load_image(struct dewlock_state *state);
