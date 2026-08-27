/*
 * Build a retailOS-acceptable music library on the emulated iPod's disk.
 *
 * Why this exists: the image shipped an iTunesDB whose header had
 * hashing_scheme == 0 and an all-zero hash58 field, because
 * iPod_Control/Device/SysInfo declared FirewireGuid: 0000000000000000. The
 * Classic 6G is in the generation that validates the hash58 signature over the
 * database, keyed by the device's FireWire GUID, so an unsigned database is
 * rejected wholesale -- which is what "no songs" on the player means.
 *
 * The device's real GUID is not a guess: it is in the SysCfg block at the start
 * of this device's own NOR (assets/classic6g/bootflash_nor_1MB.bin), tag 'FwId'
 * at offset 0x2c, whose 8 data bytes little-endian read 000A270013118B17 --
 * 00:0A:27 being Apple's FireWire OUI. Writing that into SysInfo and letting
 * libgpod re-sign is a change to the iPod's own filesystem, exactly what iTunes
 * does on a sync. No firmware is patched.
 *
 * Input: a TSV on stdin, one track per line:
 *     path <TAB> title <TAB> artist <TAB> album <TAB> ms <TAB> bitrate <TAB> rate
 *
 * Build: gcc -O2 -o mklib mklib.c $(pkg-config --cflags --libs libgpod-1.0)
 * Run:   ./mklib <mountpoint> < tracks.tsv
 */
#include <gpod/itdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static char *field(char **p)
{
    char *s = *p;
    char *t = strchr(s, '\t');
    if (t) {
        *t = '\0';
        *p = t + 1;
    } else {
        *p = s + strlen(s);
    }
    return s;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: mklib <ipod-mountpoint> < tracks.tsv\n");
        return 2;
    }
    const char *mount = argv[1];
    GError *err = NULL;

    Itdb_iTunesDB *db = itdb_parse(mount, &err);
    if (!db) {
        fprintf(stderr, "itdb_parse failed: %s\n", err ? err->message : "?");
        return 1;
    }
    printf("parsed: %u tracks, %u playlists\n",
           g_list_length(db->tracks), g_list_length(db->playlists));

    /*
     * Report what libgpod thinks this device is. If the generation is not
     * recognised it will pick checksum type NONE and write an unsigned
     * database again -- silently. Better to see it.
     */
    if (db->device) {
        const Itdb_IpodInfo *info = itdb_device_get_ipod_info(db->device);
        if (info) {
            printf("device: model=%s generation=%d\n",
                   info->model_number ? info->model_number : "?",
                   (int)info->ipod_generation);
        }
        gchar *fwid = itdb_device_get_sysinfo(db->device, "FirewireGuid");
        printf("device: FirewireGuid=%s\n", fwid ? fwid : "(none)");
        g_free(fwid);
    } else {
        printf("device: NONE -- libgpod found no SysInfo, it cannot sign\n");
    }

    Itdb_Playlist *mpl = itdb_playlist_mpl(db);
    if (!mpl) {
        fprintf(stderr, "no master playlist\n");
        return 1;
    }

    char line[4096];
    int added = 0;
    while (fgets(line, sizeof line, stdin)) {
        char *nl = strchr(line, '\n');
        if (nl) {
            *nl = '\0';
        }
        if (line[0] == '\0') {
            continue;
        }
        char *p = line;
        char *path   = field(&p);
        char *title  = field(&p);
        char *artist = field(&p);
        char *album  = field(&p);
        char *ms     = field(&p);
        char *br     = field(&p);
        char *rate   = field(&p);

        struct stat st;
        if (stat(path, &st) != 0) {
            fprintf(stderr, "  skip (missing): %s\n", path);
            continue;
        }

        Itdb_Track *tr = itdb_track_new();
        tr->title      = g_strdup(title);
        tr->artist     = g_strdup(artist);
        tr->album      = g_strdup(album);
        tr->genre      = g_strdup("Pop");
        tr->filetype   = g_strdup("MPEG audio file");
        tr->mediatype  = ITDB_MEDIATYPE_AUDIO;
        tr->size       = (guint32)st.st_size;
        tr->tracklen   = atoi(ms);
        tr->bitrate    = atoi(br);
        tr->samplerate = atoi(rate);
        tr->track_nr   = added + 1;
        tr->visible    = 1;

        itdb_track_add(db, tr, -1);
        itdb_playlist_add_track(mpl, tr, -1);

        err = NULL;
        if (!itdb_cp_track_to_ipod(tr, path, &err)) {
            fprintf(stderr, "  copy failed for %s: %s\n",
                    path, err ? err->message : "?");
            itdb_playlist_remove_track(mpl, tr);
            itdb_track_remove(tr);
            continue;
        }
        printf("  added: %-40.40s  %d ms  -> %s\n",
               tr->title, tr->tracklen,
               tr->ipod_path ? tr->ipod_path : "?");
        added++;
    }

    printf("adding %d track(s); writing database\n", added);
    err = NULL;
    if (!itdb_write(db, &err)) {
        fprintf(stderr, "itdb_write failed: %s\n", err ? err->message : "?");
        return 1;
    }
    printf("wrote: %u tracks total\n", g_list_length(db->tracks));
    itdb_free(db);
    return 0;
}
