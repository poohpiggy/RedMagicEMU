#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Load the packaged module without opening a framebuffer or modifying a display. */
struct module_methods { void *open; };
struct module {
    uint32_t tag;
    uint16_t module_api_version, hal_api_version;
    const char *id, *name, *author;
    struct module_methods *methods;
};

int main(int argc, char **argv) {
    if (argc != 2) return 1;
    void *library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library) { fprintf(stderr, "%s\n", dlerror()); return 2; }
    const struct module *module = dlsym(library, "HMI");
    if (!module || module->tag != 0x484d4f44 || !module->id || strcmp(module->id, "hwcomposer") ||
        !module->methods || !module->methods->open) return 3;
    dlclose(library);
    return 0;
}
