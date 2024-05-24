#include <stdlib.h>
#include <argp.h>

/* Program documentation. */
static char doc[] =
    "FOME Simulator -- https://wiki.fome.tech/";

/* A description of the arguments we accept. */
static char args_doc[] = "[TIMEOUT]";

/* The options we understand. */
static struct argp_option options[] = {
    {"verbose", 'v', 0,         0, "Produce verbose output (default)", 0 },
    {"socketcan-device",
                'd', "DEVICE",  0, "SocketCAN DEVICE (default: can0) to use", 0 },
    { 0, 0, 0, 0, 0, 0 }
};

/* Used by main to communicate with parse_opt. */
struct arguments
{
    int timeout;
    int verbose;
    char * socketcanDevice;
};

/* Parse a single option. */
static error_t
parse_opt(int key, char * arg, struct argp_state * state)
{
    /* Get the input argument from argp_parse, which we
       know is a pointer to our arguments structure. */
    struct arguments * arguments = (struct arguments *)state->input;

    switch (key) {
        case 'v':
            arguments->verbose = 1;
            break;
        case 'd':
            arguments->socketcanDevice = arg;
            break;

        case ARGP_KEY_ARG:
            if (state->arg_num >= 1) {
                /* Too many arguments. */
                argp_usage(state);
            }
            arguments->timeout = atoi(arg);
            break;

        case ARGP_KEY_END:
        case ARGP_KEY_NO_ARGS:
            break;

        default:
            return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

/* Our argp parser. */
static struct argp argp = { options, parse_opt, args_doc, doc, 0, 0, 0 };
