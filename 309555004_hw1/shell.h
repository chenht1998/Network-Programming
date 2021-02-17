#ifndef SHELL_H
#define SHELL_H

int setenv();
int printenv();
int exit();

int cat();
int ls();
int noop();
int number();
int removetag();
int removetag0();

int printErr(char errContent);

#endif