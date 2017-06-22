/* Not used for now, i.e. for single file replay trial.
 *
 * Later, if needed, the "device" angle can be mapped to "hostname" angle
 * because multiple host (VM) I/O traces may need to be executed parallely,
 * for example, boot up of 20 identical VMs.
 * But still we may not need device mappings and all.
 */

/* 
 * ========================================================================
 * ==== INPUT DEVICE HANDLERS =============================================
 * ========================================================================
 */

/**
 * add_input_dev - Add a device ('sd*') to the list of devices to handle
 */
static void add_input_dev(char *devnm)
{
    struct list_head *p;
    struct dev_info *dip;

    __list_for_each(p, &input_devs) {
        dip = list_entry(p, struct dev_info, head);
        if (strcmp(dip->devnm, devnm) == 0)
            return;
    }

    dip = malloc(sizeof(*dip));
    dip->devnm = strdup(devnm);
    list_add_tail(&dip->head, &input_devs);
}

/**
 * rem_input_dev - Remove resources associated with this device
 */
static void rem_input_dev(struct dev_info *dip)
{
    list_del(&dip->head);
    free(dip->devnm);
    free(dip);
}

static void find_input_devs(char *idir)
{
    struct dirent *ent;
    DIR *dir = opendir(idir);

    if (dir == NULL) {
        fatal(idir, ERR_ARGS, "Unable to open %s\n", idir);
        /*NOTREACHED*/
    }

    while ((ent = readdir(dir)) != NULL) {
        char *p, *dsf = malloc(256);

        if (strstr(ent->d_name, ".pdatadump.") == NULL)
            continue;

        dsf = strdup(ent->d_name);
        p = index(dsf, '.');
        assert(p != NULL);
        *p = '\0';
        add_input_dev(dsf);
        free(dsf);
    }

    closedir(dir);
}

