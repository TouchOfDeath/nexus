#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

#define WIN_STD_INPUT_HANDLE  ((uint32_t)-10)
#define WIN_STD_OUTPUT_HANDLE ((uint32_t)-11)
#define WIN_STD_ERROR_HANDLE  ((uint32_t)-12)

static int translate_handle(uint64_t h) {
    if (h == (uint64_t)(int64_t)(int32_t)WIN_STD_INPUT_HANDLE || h == 100) return 0;
    if (h == (uint64_t)(int64_t)(int32_t)WIN_STD_OUTPUT_HANDLE || h == 101) return 1;
    if (h == (uint64_t)(int64_t)(int32_t)WIN_STD_ERROR_HANDLE || h == 102) return 2;
    return (int)h;
}

uint64_t __attribute__((ms_abi, force_align_arg_pointer)) win_GetStdHandle(uint32_t nStdHandle) {
    if (nStdHandle == WIN_STD_INPUT_HANDLE) return 100;
    if (nStdHandle == WIN_STD_OUTPUT_HANDLE) return 101;
    if (nStdHandle == WIN_STD_ERROR_HANDLE) return 102;
    return (uint64_t)-1;
}

uint32_t __attribute__((ms_abi, force_align_arg_pointer)) win_WriteFile(uint64_t hFile, const void* lpBuffer, uint32_t nNumberOfBytesToWrite, uint32_t* lpNumberOfBytesWritten, void* lpOverlapped) {
    int fd = translate_handle(hFile);
    ssize_t ret = write(fd, lpBuffer, nNumberOfBytesToWrite);
    if (ret >= 0) {
        if (lpNumberOfBytesWritten) *lpNumberOfBytesWritten = (uint32_t)ret;
        return 1;
    }
    return 0;
}

uint32_t __attribute__((ms_abi, force_align_arg_pointer)) win_ReadFile(uint64_t hFile, void* lpBuffer, uint32_t nNumberOfBytesToRead, uint32_t* lpNumberOfBytesRead, void* lpOverlapped) {
    int fd = translate_handle(hFile);
    ssize_t ret = read(fd, lpBuffer, nNumberOfBytesToRead);
    if (ret >= 0) {
        if (lpNumberOfBytesRead) *lpNumberOfBytesRead = (uint32_t)ret;
        return 1;
    }
    return 0;
}

void __attribute__((ms_abi, force_align_arg_pointer)) win_ExitProcess(uint32_t uExitCode) {
    exit(uExitCode);
}

void* __attribute__((ms_abi, force_align_arg_pointer)) win_VirtualAlloc(void* lpAddress, size_t dwSize, uint32_t flAllocationType, uint32_t flProtect) {
    void* ptr = mmap(lpAddress, dwSize, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) return NULL;
    return ptr;
}

uint64_t __attribute__((ms_abi, force_align_arg_pointer)) win_CreateFileA(const char* lpFileName, uint32_t dwDesiredAccess, uint32_t dwShareMode, void* lpSecurityAttributes, uint32_t dwCreationDisposition, uint32_t dwFlagsAndAttributes, void* hTemplateFile) {
    int flags = O_RDWR;
    if ((dwDesiredAccess & 0x40000000) && !(dwDesiredAccess & 0x80000000)) flags = O_WRONLY | O_CREAT | O_TRUNC;
    else if (!(dwDesiredAccess & 0x40000000) && (dwDesiredAccess & 0x80000000)) flags = O_RDONLY;
    else if (dwCreationDisposition == 2) flags = O_RDWR | O_CREAT | O_TRUNC;
    else flags = O_RDWR | O_CREAT;
    
    int fd = open(lpFileName, flags, 0755);
    if (fd < 0) return (uint64_t)-1;
    return (uint64_t)fd;
}

uint32_t __attribute__((ms_abi, force_align_arg_pointer)) win_CloseHandle(uint64_t hObject) {
    int fd = translate_handle(hObject);
    if (fd >= 3) close(fd);
    return 1;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: %s <pe_binary>\n", argv[0]);
        return 1;
    }

    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror("fopen"); return 1; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* raw = malloc(sz);
    if (fread(raw, 1, sz, f) != (size_t)sz) { perror("fread"); return 1; }
    fclose(f);

    uint32_t pe_offset = *(uint32_t*)(raw + 0x3C);
    uint8_t* pe_hdr = raw + pe_offset;
    uint16_t num_sections = *(uint16_t*)(pe_hdr + 6);
    uint16_t opt_hdr_size = *(uint16_t*)(pe_hdr + 20);
    uint8_t* opt_hdr = pe_hdr + 24;

    uint64_t image_base = *(uint64_t*)(opt_hdr + 24);
    uint32_t entry_point_rva = *(uint32_t*)(opt_hdr + 16);
    uint32_t size_of_image = *(uint32_t*)(opt_hdr + 56);

    void* base = mmap((void*)image_base, size_of_image + 0x100000, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (base == MAP_FAILED) {
        perror("mmap fixed failed");
        return 1;
    }

    uint8_t* sec_headers = opt_hdr + opt_hdr_size;
    for (int i = 0; i < num_sections; i++) {
        uint8_t* sec = sec_headers + i * 40;
        uint32_t virt_size = *(uint32_t*)(sec + 8);
        uint32_t virt_addr = *(uint32_t*)(sec + 12);
        uint32_t raw_size = *(uint32_t*)(sec + 16);
        uint32_t raw_ptr = *(uint32_t*)(sec + 20);
        memcpy((uint8_t*)base + virt_addr, raw + raw_ptr, raw_size);
    }

    // Resolve imports (Import Directory at opt_hdr + 120)
    uint32_t import_dir_rva = *(uint32_t*)(opt_hdr + 120);
    if (import_dir_rva != 0) {
        uint8_t* desc = (uint8_t*)base + import_dir_rva;
        while (1) {
            uint32_t orig_thunk = *(uint32_t*)(desc);
            uint32_t name_rva = *(uint32_t*)(desc + 12);
            uint32_t first_thunk = *(uint32_t*)(desc + 16);
            if (orig_thunk == 0 && first_thunk == 0) break;

            uint64_t* thunk = (uint64_t*)((uint8_t*)base + first_thunk);
            uint64_t* lookup = orig_thunk ? (uint64_t*)((uint8_t*)base + orig_thunk) : thunk;

            for (int idx = 0; lookup[idx] != 0; idx++) {
                uint64_t val = lookup[idx];
                if (!(val & (1ULL << 63))) {
                    uint8_t* name_entry = (uint8_t*)base + (uint32_t)val;
                    const char* func_name = (const char*)(name_entry + 2);
                    void* fn_ptr = NULL;
                    if (strcmp(func_name, "GetStdHandle") == 0) fn_ptr = (void*)win_GetStdHandle;
                    else if (strcmp(func_name, "WriteFile") == 0) fn_ptr = (void*)win_WriteFile;
                    else if (strcmp(func_name, "ReadFile") == 0) fn_ptr = (void*)win_ReadFile;
                    else if (strcmp(func_name, "ExitProcess") == 0) fn_ptr = (void*)win_ExitProcess;
                    else if (strcmp(func_name, "VirtualAlloc") == 0) fn_ptr = (void*)win_VirtualAlloc;
                    else if (strcmp(func_name, "CreateFileA") == 0) fn_ptr = (void*)win_CreateFileA;
                    else if (strcmp(func_name, "CloseHandle") == 0) fn_ptr = (void*)win_CloseHandle;
                    thunk[idx] = (uint64_t)fn_ptr;
                }
            }
            desc += 20;
        }
    }

    void (*entry_point)(void) = (void (*)(void))(image_base + entry_point_rva);
    entry_point();

    return 0;
}
