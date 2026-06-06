#ifndef LAUNCHER_H
#define LAUNCHER_H

void launcher_init(void);
void launcher_render(void);
void launcher_handle_click(int mx, int my);
void launcher_update(void);
int launcher_is_open(void);

#endif