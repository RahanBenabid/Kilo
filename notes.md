`make` checks the last time has been modified, so it doesn't have to compile files that have already been compiled with no change, this is useful when compiling large projects.

`read()` returns the number of bytes that it read, and when it returns 0, it means it reached the end of file. that's why `cmd+d` can end a program that is reading from the standard input.

the terminal starts in **canonical mode** or **cooked mode**. keyboard input is only sent to the program once you press `enter`. but we want raw mode, because we want to register and process every keypress, unlike cooked mode that doesn't do it until enter is pressed, and the user can press backspace (delete) and a bunch of other studd

# Entering Raw Mode

We use the `unistd.h` library to use `read()` and `STDIN_FILENO` to be able to read 1 byte from the standard library until there is no more bytes to read, the `read()` function returns the number of bytes that it read, then returns 0 when it reaches the end of file.

# Ctrl Key

in the terminal, the `ctrl` key strips the bits 5 and 6 from of the ASCII code of whatever key you're pressing, this was probably made on purpose, in our code, we do the same thing, we define `CTRL_KEY` that makes a bitwise and with the character value we just pressed. here is an example:

Since in ASCII the bits from 0->31 represent the control character, and the bits and the bits from 60->90 represent the alphabet from a to Z

```c
'B' & 0x1f = 0b01000010 & 0b00011111 = 0b00000010 = 2 // ctrl + b
```

So we strip the 5 first bits.

a similar concept is done for uppercase to lowercase. you switch the 5th bit of the representation of a letter.

# Escape Sequences

Escape sequences always start with an escape character (27) (`0x1b`) followed by a [ character. Escape sequences instruct the terminal to do various text formatting tasks, such as coloring text, moving the cursor around, and clearing parts of the screen.

## VT100

this escape sequence will be used in the project, supported by most modern terminals, unless you're a jobless ahh who uses sunOS or something, job-off man, for example the `\x1b[2J` sequence to clear the screen.
