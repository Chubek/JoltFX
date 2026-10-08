#ifndef JOLTFX_CLI_COMMANDS_H
#define JOLTFX_CLI_COMMANDS_H

/* Exit codes: 0 success, 1 usage or runtime failure. */
int cmd_compile(int argc, char **argv);
int cmd_verify(int argc, char **argv);
int cmd_effects(int argc, char **argv);
int cmd_info(int argc, char **argv);
int cmd_render(int argc, char **argv);
int cmd_export(int argc, char **argv);
int cmd_capabilities(void);

/* Colour grading, node compositing and non-linear editing. */
int cmd_nodes(int argc, char **argv);
int cmd_lut(int argc, char **argv);
int cmd_render_graph(int argc, char **argv);
int cmd_render_sequence(int argc, char **argv);
int cmd_edit(int argc, char **argv);
int cmd_nle(int argc, char **argv);
int cmd_compose(int argc, char **argv);
int cmd_scene3d(int argc,char **argv);
int cmd_media_export(int argc, char **argv);
int cmd_color(int argc, char **argv, int calibration);
int cmd_project(int argc, char **argv);
int cmd_plugins(int argc,char **argv);
int cmd_scripts(int argc,char **argv);

/* Help output shared with main.c. */
void print_usage(void);
void print_command_help(const char *command);

#endif
