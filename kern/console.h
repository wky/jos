/* See COPYRIGHT for copyright information. */

#ifndef _CONSOLE_H_
#define _CONSOLE_H_
#ifndef JOS_KERNEL
# error "This is a JOS kernel header; user programs should not #include it"
#endif

#include <inc/types.h>

#define MONO_BASE	0x3B4
#define MONO_BUF	0xB0000
#define CGA_BASE	0x3D4
#define CGA_BUF		0xB8000

#define CRT_ROWS	25
#define CRT_COLS	80
#define CRT_SIZE	(CRT_ROWS * CRT_COLS)

enum COLOR {
  C_BLACK=0,
  C_BLUE,   // 1      
  C_GREEN,  // 2
  C_CYAN,   // 3
  C_RED,    // 4
  C_MAGENTA,// 5
  C_BROWN,  // 6
  C_LIGHT_GRAY, // 7
  C_GRAY,       // 8
  C_LIGHT_BLUE, // 9
  C_LIGHT_GREEN,// 10
  C_LIGHT_CYAN, // 11
  C_LIGHT_RED,  // 12
  C_LIGHT_MAGEMTA,  // 13
  C_YELLOW,   //14
  C_WHITE     //15
};
void cons_init(void);
int cons_getc(void);

void kbd_intr(void); // irq 1
void serial_intr(void); // irq 4
void cputchar_opt(int c, int foreground, int background);

#endif /* _CONSOLE_H_ */
