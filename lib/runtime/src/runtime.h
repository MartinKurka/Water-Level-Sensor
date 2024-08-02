#ifndef RUNTIME_H
#define RUNTIME_H

struct Runtime {
    unsigned long days;
    unsigned long hours;
    unsigned long minutes;
    unsigned long seconds;
};

// Function declaration
Runtime getRuntime();
#endif // RUNTIME_H