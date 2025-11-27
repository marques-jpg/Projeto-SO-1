#include <dirent.h>
#include <stdio.h>


static int has_extension(const char *filename, const char *ext) {
    const char *dot = strrchr(filename, '.');
    if (!dot || dot == filename) {
        return 0;
    }
    return (strcmp(dot, ext) == 0);
}


void filefinder(const char *dirpath){
    DIR *dirp;
    struct dirent *dp;
    dirp = opendir(dirpath);

    if(dirp == NULL) {
        errMsg("opendir failed on '%s'", dirpath);
        return;
    }   

    for (;;) {
        dp = readdir(dirp);
        if (dp == NULL)
        break;

        if (strcmp(dp->d_name, ".") == 0 || strcmp(dp->d_name, "..") == 0)
        continue; /* Skip . and .. */

        if(has_extension(dp->d_name, ".lvl")){

        }
        if(has_extension(dp->d_name, ".p")){

        }

        if(has_extension(dp->d_name, ".m")){
        }
    }
    
}