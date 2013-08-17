/*
 * See the file "COPYING" for the full license governing this code.
 *
 * For an overview of how argument handling was originally designed in dcp,
 * please see the blog post at:
 *
 * <http://www.bringhurst.org/2012/12/16/file-copy-tool-argument-handling.html>
 */

#include "handle_args.h"
#include "treewalk.h"
#include "dcp.h"
#include "bayer.h"

#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/** Where we should store options specified by the user. */
DCOPY_options_t DCOPY_user_opts;

typedef struct param_file {
    char* orig;                /* original path as specified by user */
    char* path;                /* reduced path, but still includes symlinks */
    int   path_stat_valid;     /* flag to indicate whether path_stat is valid */
    struct stat64 path_stat;   /* stat of path */
    char* target;              /* fully resolved path, no more symlinks */
    int   target_stat_valid;   /* flag to indicate whether target_stat is valid */
    struct stat64 target_stat; /* stat of target path */
} param_file_t;

static param_file_t  dest_param;
static param_file_t* src_params;
static int num_src_params;

/* initialize fields in param */
static void DCOPY_param_init(param_file_t* param)
{
    /* initialize all fields */
    if(param != NULL) {
        param->orig = NULL;
        param->path = NULL;
        param->path_stat_valid = 0;
        param->target = NULL;
        param->target_stat_valid = 0;
    }

    return;
}

/* set fields in param according to path */
static void DCOPY_param_set(const char* path, param_file_t* param)
{
    /* initialize all fields */
    DCOPY_param_init(param);

    if(path != NULL) {
        /* make a copy of original path */
        param->orig = bayer_strdup(path, "original path", __FILE__, __LINE__);

        /* get absolute path and remove ".", "..", consecutive "/",
         * and trailing "/" characters */
        bayer_path* p = bayer_path_from_str(path);
        if(! bayer_path_is_absolute(p)) {
            char cwd[PATH_MAX];
            bayer_getcwd(cwd, PATH_MAX);
            bayer_path_prepend_str(p, cwd);
        }
        bayer_path_reduce(p);
        param->path = bayer_path_strdup(p);
        bayer_path_delete(&p);

        /* get stat info for simplified path */
        if(lstat64(param->path, &param->path_stat) == 0) {
            param->path_stat_valid = 1;
        }

        /* TODO: we use realpath below, which is nice since it takes out
         * ".", "..", symlinks, and adds the absolute path, however, it
         * fails if the file/directory does not already exist, which is
         * often the case for dest path. */

        /* resolve any symlinks */
        char target[PATH_MAX];
        if(realpath(path, target) != NULL) {
            /* make a copy of resolved name */
            param->target = bayer_strdup(target, "target path", __FILE__, __LINE__);

            /* get stat info for resolved path */
            if(lstat64(param->target, &param->target_stat) == 0) {
                param->target_stat_valid = 1;
            }
        }
    }

    return;
}

/* free memory associated with param */
static void DCOPY_param_free(param_file_t* param)
{
    if(param != NULL) {
        /* free all mememory */
        bayer_free(&param->orig);
        bayer_free(&param->path);
        bayer_free(&param->target);

        /* initialize all fields */
        DCOPY_param_init(param);
    }
    return;
}

/**
 * Determine if the destination path is a file or directory.
 *
 * It does this by first checking to see if an object is actually at the
 * destination path. If an object isn't already at the destination path, we
 * examine the source path(s) to determine the type of what the destination
 * path will be.
 *
 * @return true if the destination should be a directory, false otherwise.
 */

/**
 * Analyze all file path inputs and place on the work queue.
 *
 * We start off with all of the following potential options in mind and prune
 * them until we figure out what situation we have.
 *
 * Libcircle only calls this function from rank 0, so there's no need to check
 * the current rank here.
 *
 * Source must overwrite destination.
 *   - Single file to single file
 *
 * Must return an error. Impossible condition.
 *   - Single directory to single file
 *   - Many file to single file
 *   - Many directory to single file
 *   - Many directory and many file to single file
 *
 * All Sources must be placed inside destination.
 *   - Single file to single directory
 *   - Single directory to single directory
 *   - Many file to single directory
 *   - Many directory to single directory
 *   - Many file and many directory to single directory
 *
 * @param handle the libcircle primary queue handle.
 */
void DCOPY_enqueue_work_objects(CIRCLE_handle* handle)
{
    int i;

    /* count number of readable source paths */
    int num_readable = 0;
    for(i = 0; i < num_src_params; i++) {
        char* path = src_params[i].path;
        if(access(path, R_OK) == 0) {
            num_readable++;
        }
        else {
            char* orig = src_params[i].orig;
            LOG(DCOPY_LOG_ERR, "Could not read `%s'. %s",
                orig, strerror(errno));
        }
    }

    /* verify that we could at least one source path */
    if(num_readable < 1) {
        LOG(DCOPY_LOG_ERR, "At least one valid source must be specified.");
        DCOPY_abort(EXIT_FAILURE);
    }

    /*
     * First we need to determine if the last argument is a file or a directory.
     * We first attempt to see if the last argument already exists on disk. If it
     * doesn't, we then look at the sources to see if we can determine what the
     * last argument should be.
     */

    bool dest_exists = false;
    bool dest_is_dir = false;
    bool dest_is_file = false;
    bool dest_is_link_to_dir = false;
    bool dest_is_link_to_file = false;
    bool dest_required_to_be_dir = false;

    /* check whether dest exists and if so determine its type */
    if(dest_param.path_stat_valid) {
        /* we could stat dest path, so something is there */
        dest_exists = true;

        /* now determine its type */
        if(S_ISDIR(dest_param.path_stat.st_mode)) {
            /* dest is a directory */
            dest_is_dir  = true;
        }
        else if (S_ISREG(dest_param.path_stat.st_mode)) {
            /* dest is a file */
            dest_is_file = true;
        }
        else if (S_ISLNK(dest_param.path_stat.st_mode)) {
            /* dest is a symlink, but to what? */
            if (dest_param.target_stat_valid) {
                /* target of the symlink exists, determine what it is */
                if(S_ISDIR(dest_param.target_stat.st_mode)) {
                    /* dest is link to a directory */
                    dest_is_link_to_dir = true;
                }
                else if (S_ISREG(dest_param.target_stat.st_mode)) {
                    /* dest is link to a file */
                    dest_is_link_to_file = true;
                }
                else {
                    /* unsupported type */
                    LOG(DCOPY_LOG_ERR, "Unsupported filetype `%s' --> `%s'.",
                        dest_param.orig, dest_param.target);
                    DCOPY_abort(EXIT_FAILURE);
                }
            }
        }
        else {
            /* unsupported type */
           LOG(DCOPY_LOG_ERR, "Unsupported filetype `%s'.",
               dest_param.orig);
           DCOPY_abort(EXIT_FAILURE);
        }
    }

    /* TODO: check that dest is writable */

    /* determine whether caller *requires* copy into dir */

    /* TODO: if caller specifies dest/ or dest/. */

    /* if caller specifies more than one source,
     * then dest has to be a directory */
    if(num_src_params > 1) {
        dest_required_to_be_dir = true;
    }

    /* if caller requires dest to be a directory, and if dest does not
     * exist or it does it exist but it's not a directory, then abort */
    if(dest_required_to_be_dir &&
       (!dest_exists || (!dest_is_dir && !dest_is_link_to_dir)))
    {
        LOG(DCOPY_LOG_ERR, "Destination is not a directory '%s'.", dest_param.orig);
        DCOPY_abort(EXIT_FAILURE);
    }

    if(dest_required_to_be_dir || dest_is_dir || dest_is_link_to_dir) {
        /* copy source params into directory */
        LOG(DCOPY_LOG_DBG, "Infered that the destination is a directory.");

        /* enqueue each source param */
        for(i = 0; i < num_src_params; i++) {
            char* src_path = src_params[i].path;
            LOG(DCOPY_LOG_DBG, "Enqueueing source path `%s'.", src_path);

            /* TODO: skip sources we can't read */

            /*
             * If the destination directory already exists, we want to place
             * new files inside it. To do this, we send a path fragment along
             * with the source path message and append it to the options dest
             * path whenever the options dest path is used.
             */

            /* get basename of src path. */
            bayer_path* p = bayer_path_from_str(src_path);
            bayer_path_basename(p);
            char* src_path_basename = bayer_path_strdup(p);
            bayer_path_delete(&p);

            uint16_t src_len = (uint16_t)strlen(src_path);
            char* op = DCOPY_encode_operation(TREEWALK, 0, src_path,
                                              src_len, src_path_basename, 0);
            handle->enqueue(op);
            free(op);

            bayer_free(&src_path_basename);
        }
    }
    else {
        /* to get here, there must be one source, and if dir exists,
         * is is not a directory or a link to a directory */
        LOG(DCOPY_LOG_DBG, "Infered that the destination is a file.");

        /* TODO: if dest exists, check that it's a file or link */

        char* src_path = src_params[0].path;
        LOG(DCOPY_LOG_DBG, "Enqueueing single source path `%s'.", src_path);

        uint16_t src_len = (uint16_t)strlen(src_path);
        char* op = DCOPY_encode_operation(TREEWALK, 0, src_path,
                                          src_len, NULL, 0);

        handle->enqueue(op);
        free(op);
    }
}

/**
 * Rank 0 passes in pointer to string to be bcast -- all others pass in a
 * pointer to a string which will be newly allocated and filled in with copy.
 */
static bool DCOPY_bcast_str(char* send, char** recv)
{
    /* First, we broadcast the number of characters in the send string. */
    int len = 0;

    if(recv == NULL) {
        LOG(DCOPY_LOG_ERR, "Attempted to receive a broadcast into invalid memory. " \
                "Please report this as a bug!");
        return false;
    }

    if(CIRCLE_global_rank == 0) {
        if(send != NULL) {
            len = (int)(strlen(send) + 1);

            if(len > CIRCLE_MAX_STRING_LEN) {
                LOG(DCOPY_LOG_ERR, "Attempted to send a larger string (`%d') than what "
                        "libcircle supports. Please report this as a bug!", len);
                return false;
            }
        }
    }

    if(MPI_SUCCESS != MPI_Bcast(&len, 1, MPI_INT, 0, MPI_COMM_WORLD)) {
        LOG(DCOPY_LOG_DBG, "While preparing to copy, broadcasting the length of a string over MPI failed.");
        return false;
    }

    /* If the string is non-zero bytes, allocate space and bcast it. */
    if(len > 0) {
        /* allocate space to receive string */
        *recv = (char*) malloc((size_t)len);

        if(*recv == NULL) {
            LOG(DCOPY_LOG_ERR, "Failed to allocate string of %d bytes", len);
            return false;
        }

        /* Broadcast the string. */
        if(CIRCLE_global_rank == 0) {
            strncpy(*recv, send, len);
        }

        if(MPI_SUCCESS != MPI_Bcast(*recv, len, MPI_CHAR, 0, MPI_COMM_WORLD)) {
            LOG(DCOPY_LOG_DBG, "While preparing to copy, broadcasting the length of a string over MPI failed.");
            return false;
        }

    }
    else {
        /* Root passed in a NULL value, so set the output to NULL. */
        *recv = NULL;
    }

    return true;
}

/**
 * Convert the destination to an absolute path and check sanity.
 */
static void DCOPY_parse_dest_path(char* path)
{
    /* identify destination path */
    char dest_path[PATH_MAX];

    /* standardize destination path */
    if(CIRCLE_global_rank == 0) {
        DCOPY_param_set(path, &dest_param);
        strncpy(dest_path, dest_param.path, sizeof(dest_path));
    }

    /* Copy the destination path to user opts structure on each rank. */
    if(!DCOPY_bcast_str(dest_path, &DCOPY_user_opts.dest_path)) {
        LOG(DCOPY_LOG_ERR, "Could not send the proper destination path to other nodes (`%s'). " \
            "The MPI broadcast operation failed. %s", \
            path, strerror(errno));
        DCOPY_abort(EXIT_FAILURE);
    }

    return;
}

/**
 * Grab the source paths.
 */
static void DCOPY_parse_src_paths(char** argv, \
                                  int last_arg_index, \
                                  int optind_local)
{
    /* allocate memory to store pointers to source path info */
    src_params = NULL;
    num_src_params = last_arg_index - optind_local;

    /* only rank 0 resolves the path(s) */
    if(CIRCLE_global_rank == 0) {
        /* allocate space to record info about each source */
        if(num_src_params > 0) {
            size_t src_params_bytes = (size_t)(num_src_params) * sizeof(param_file_t);
            src_params = (param_file_t*) bayer_malloc(src_params_bytes, "sources", __FILE__, __LINE__);
        }

        /* record standardized paths and stat info for each source. */
        int opt_index;
        for(opt_index = optind_local; opt_index < last_arg_index; opt_index++) {
            char* path = argv[opt_index];
            int idx = opt_index - optind_local;
            DCOPY_param_set(path, &src_params[idx]);
        }
    }

    return;
}

/**
 * Parse the source and destination paths that the user has provided.
 */
void DCOPY_parse_path_args(char** argv, \
                           int optind_local, \
                           int argc)
{
    int num_args = argc - optind_local;
    int last_arg_index = num_args + optind_local - 1;

    if(argv == NULL || num_args < 2) {
        if(CIRCLE_global_rank == 0) {
            DCOPY_print_usage(argv);
            LOG(DCOPY_LOG_ERR, "You must specify a source and destination path.");
        }

        DCOPY_exit(EXIT_FAILURE);
    }

    /* Grab the destination path. */
    DCOPY_parse_dest_path(argv[last_arg_index]);

    /* Grab the source paths. */
    DCOPY_parse_src_paths(argv, last_arg_index, optind_local);
}

/* frees resources allocated in call to parse_path_args() */
void DCOPY_free_path_args()
{
    /* only rank 0 allocated memory */
    if(CIRCLE_global_rank == 0) {
        /* free memory associated with destination path */
        DCOPY_param_free(&dest_param);

        /* free memory associated with source paths */
        int i;
        for(i = 0; i < num_src_params; i++) {
            DCOPY_param_free(&src_params[i]);
        }
        num_src_params = 0;
        bayer_free(&src_params);
    }
}

/* EOF */
