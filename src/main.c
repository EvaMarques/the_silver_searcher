#include <stdarg.h>
#include <stdio.h>
#include <sys/dir.h>
#include <sys/types.h>
#include <dirent.h>
#include <pcre.h>
#include <string.h>
#include <stdlib.h>

#include "log.h"
#include "options.h"

cli_options opts;

const int MAX_SEARCH_DEPTH = 8;

const char *evil_hardcoded_ignore_files[] = {
    ".",
    "..",
    NULL
};

int filename_filter(struct dirent *dir) {
    //regex = pcre_compile();
/*    if (dir->d_type != DT_REG && dir->d_type != DT_DIR) {
        log_debug("file %s ignored becaused of type", dir->d_name);
        return(0);
    }
    */
    for (int i = 0; evil_hardcoded_ignore_files[i] != NULL; i++) {
        if (strcmp(dir->d_name, evil_hardcoded_ignore_files[i]) == 0) {
            log_debug("file %s ignored because of name", dir->d_name);
            return(0);
        }
    }

    log_debug("Yes %s", dir->d_name);
    return(1);
};

//TODO: append matches to some data structure instead of just printing them out
// then there can be sweet summaries of matches/files scanned/time/etc
int search_dir(pcre *re, const char* path, const int depth) {
    //TODO: don't just die. also make max depth configurable
    if(depth > MAX_SEARCH_DEPTH) {
        log_err("Search depth greater than %i, giving up.", depth);
        exit(1);
    }
    struct dirent **dir_list = NULL;
    struct dirent *dir = NULL;
    int results = 0;

    FILE *fp = NULL;
    int f_len;
    size_t r_len;
    char *buf = NULL;
    int rv = 0;

    results = scandir(path, &dir_list, &filename_filter, &alphasort);
    if (results == 0)
    {
        log_debug("No results found");
        return(0);
    }
    else if (results == -1) {
        log_err("Error opening directory %s", path);
        return(0);
    }

    char *dir_full_path = NULL;
    size_t path_length = 0;

    for (int i=0; i<results; i++) {
        dir = dir_list[i];
        path_length = (size_t)(strlen(path) + strlen(dir->d_name) + 2); // 2 for slash and null char
        dir_full_path = malloc(path_length);
        dir_full_path = strncpy(dir_full_path, path, path_length);
        dir_full_path = strncat(dir_full_path, "/", path_length);
        dir_full_path = strncat(dir_full_path, dir->d_name, path_length);

        log_debug("dir %s type %i", dir_full_path, dir->d_type);
        //TODO: scan files in current dir before going deeper
        if (dir->d_type == DT_DIR && opts.recurse_dirs) {
            log_debug("searching dir %s", dir_full_path);
            rv = search_dir(re, dir_full_path, depth + 1);
            continue;
        }
        fp = fopen(dir_full_path, "r");
        if (fp == NULL) {
            log_warn("Error opening file %s. Skipping...", dir_full_path);
            continue;
        }

        rv = fseek(fp, 0, SEEK_END);
        if (rv != 0) {
            plog(LOG_LEVEL_ERR, "fseek error");
            exit(1);
        }
        f_len = ftell(fp); //TODO: behave differently if file is HUGE. anything > 2GB will screw up this program
        if (f_len == 0) {
            log_debug("file is empty. skipping");
            goto cleanup;
        }

        rewind(fp);
        buf = (char*) malloc(sizeof(char) * f_len + 1);
        r_len = fread(buf, 1, f_len, fp);
        buf[r_len] = '\0';
        int buf_len = (int)r_len;

        int buf_offset = 0;
        int offset_vector[100]; //XXXX max number of matches in a file
        int rc = 0;
        while(buf_offset < buf_len && (rc = pcre_exec(re, NULL, buf, r_len, buf_offset, 0, offset_vector, sizeof(offset_vector))) >= 0 ) {
            log_debug("match found. file %s offset %i", dir_full_path, offset_vector[0]);
            buf_offset = offset_vector[1];
            char *buf_eol = NULL;
            buf_eol = buf + buf_offset;
            printf("%s: ", dir_full_path);
            while(*buf_eol != '\n') {
                putchar(*buf_eol);
                buf_eol++;
            }
        }

        free(buf);

        cleanup:
        fclose(fp);
        free(dir);
        free(dir_full_path);
    }

    free(dir_list);
    return(0);
};

int main(int argc, char **argv) {
    set_log_level(LOG_LEVEL_DEBUG);
    opts.casing = CASE_SENSITIVE_RETRY_INSENSITIVE;
    opts.recurse_dirs = 1;

    //TODO: use getopts and ilk
    char *query;
    // last argument is the query
    if (argc < 2) {
        log_err("Not enough arguments :P");
        exit(1);
    }

    query = malloc(strlen(argv[argc-1])+1);
    strcpy(query, argv[argc-1]);

    int rv = 0;
    int pcre_opts = 0;
    const char *pcre_err = NULL;
    char *path = "."; //TODO: get this from command line opts
    int pcre_err_offset = 0;
    pcre *re = NULL;
    re = pcre_compile(query, pcre_opts, &pcre_err, &pcre_err_offset, NULL);
    if (re == NULL) {
        log_err("pcre_compile failed");
        exit(1);
    }

    rv = search_dir(re, path, 0);

    free(re);
    free(query);
    return(0);
}
