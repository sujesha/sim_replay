
/* 
 * ========================================================================
 * ==== MAP DEVICE INTERFACES =============================================
 * ========================================================================
 */
#include <unistd.h>

/**
 * read_map_devs - Read in a set of device mapping from the provided file.
 * @file_name:  File containing device maps
 *
 * We support the notion of multiple such files being specifed on the cmd line
 */
static void read_map_devs(char *file_name)
{
    FILE *fp;
    char *from_dev, *to_dev;

    fp = fopen(file_name, "r");
    if (!fp) {
        fatal(file_name, ERR_SYSCALL, "Could not open map devs file\n");
        /*NOTREACHED*/
    }

    while (fscanf(fp, "%as %as", &from_dev, &to_dev) == 2) {
        struct map_dev *mdp = malloc(sizeof(*mdp));

        mdp->from_dev = from_dev;
        mdp->to_dev = to_dev;
        list_add_tail(&mdp->head, &map_devs);
    }

    fclose(fp);
}

/**
 * release_map_devs - Release resources associated with device mappings.
 */
static void release_map_devs(void)
{
    struct list_head *p, *q;

    list_for_each_safe(p, q, &map_devs) {
        struct map_dev *mdp = list_entry(p, struct map_dev, head);

        list_del(&mdp->head);

        free(mdp->from_dev);
        free(mdp->to_dev);
        free(mdp);
    }
}

/**
 * map_dev - Return the mapped device for that specified
 * @from_dev:   Device name as seen on recorded system
 *
 * Note: If there is no such mapping, we return the same name.
 */
static char *map_dev(char *from_dev)
{
    struct list_head *p;

    __list_for_each(p, &map_devs) {
        struct map_dev *mdp = list_entry(p, struct map_dev, head);

        if (strcmp(from_dev, mdp->from_dev) == 0)
            return mdp->to_dev;
    }

    return from_dev;
}

