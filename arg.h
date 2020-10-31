/* See LICENSE file for copyright and license details */
#ifndef OPT_H
#define OPT_H

#define OPT_BEGIN(argv) \
    for (; (argv)[0]; ++(argv)) { \
        if ('-' != (argv)[0][0]) \
            break; \
        (argv)[0] += 1; \
        while ('\0' != (argv)[0][0]) { \
            (argv)[0] += 1; \
            switch ((argv)[0][-1])

#define OPT_END break;}}

/* Terminate the argument list */
#define OPT_TERM(argv) ((argv[1]) = NULL)

/* Retrieve the current flag */
#define OPT_FLAG(argv) ((argv)[0][-1])

/* Retrieve the current argument */
#define OPT_ARG(argv) ('\0' == (argv)[0][0] ? (++(argv))[0] : (argv)[0])

#endif
