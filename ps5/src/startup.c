/*
 * Wind Waker for the PlayStation 5 - what runs before the game's main.
 *
 * The game's main is Aurora's and SwitchWaker's, so the console's start-up is
 * a constructor: standard error into klog and the
 * shell's splash dismissed, and the folder SDL derives its paths from.
 *
 * SPDX-License-Identifier: MIT
 */
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <SDL3/SDL_stdinc.h>

#include "platform.h"

/* FTP runs as a different user. Repair existing directories as well as new ones. */
static void
allow_directory_access(const char *path)
{
   if (chmod(path, 0777) != 0)
      fprintf(stderr, "Cannot make directory %s FTP-accessible: %s\n", path, strerror(errno));
}

static void
repair_cache_directories(const char *path)
{
   char child[1024];
   struct dirent *entry;
   struct stat status;
   DIR *folder;

   /* Do not follow links out of the cache tree or change cache-file permissions. */
   if (lstat(path, &status) != 0 || !S_ISDIR(status.st_mode))
      return;
   allow_directory_access(path);
   folder = opendir(path);
   if (folder == NULL)
      return;
   while ((entry = readdir(folder)) != NULL) {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
         continue;
      int length = snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
      if (length >= 0 && (size_t)length < sizeof(child))
         repair_cache_directories(child);
   }
   closedir(folder);
}

/* The shader caches the title ships (res/shader-cache, filled on a console by a
 * first start: ps5/tools/link-title.sh), for a console that has none yet: Dawn's
 * cache, where Aurora opens it, and the driver's folder. Without them a first
 * start compiles every render pipeline, which takes minutes. Copied once: what
 * is there, even half of it, is the console's own from then on. */
static void
seed_file(const char *from, const char *to)
{
   char buffer[65536];
   size_t count;
   FILE *in = fopen(from, "rb");
   FILE *out = in != NULL ? fopen(to, "wb") : NULL;

   while (out != NULL && (count = fread(buffer, 1, sizeof(buffer), in)) > 0)
      fwrite(buffer, 1, count, out);
   if (out != NULL)
      fclose(out);
   if (in != NULL)
      fclose(in);
}

static void
seed_folder(const char *from, const char *to)
{
   char source[512], target[512];
   struct dirent *entry;
   struct stat status;
   DIR *folder = opendir(from);

   if (folder == NULL)
      return;
   mkdir(to, 0777);
   allow_directory_access(to);
   while ((entry = readdir(folder)) != NULL) {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
         continue;
      snprintf(source, sizeof(source), "%s/%s", from, entry->d_name);
      snprintf(target, sizeof(target), "%s/%s", to, entry->d_name);
      if (stat(source, &status) != 0)
         continue;
      if (S_ISDIR(status.st_mode))
         seed_folder(source, target);
      else
         seed_file(source, target);
   }
   closedir(folder);
}

static void
seed_shader_caches(void)
{
   struct stat status;

   if (stat("/app0/res/shader-cache/dawn_cache.db", &status) != 0)
      return;
   if (stat("/app0/user/cache/dawn_cache.db", &status) != 0) {
      seed_file("/app0/res/shader-cache/dawn_cache.db", "/app0/user/cache/dawn_cache.db");
      fprintf(stderr, "shader cache: Dawn's copied from the title\n");
   }
   if (stat("/app0/radv-shader-cache/mesa_shader_cache_db", &status) != 0) {
      seed_folder("/app0/res/shader-cache/radv-shader-cache", "/app0/radv-shader-cache");
      fprintf(stderr, "shader cache: the driver's copied from the title\n");
   }
}

__attribute__((constructor)) static void
ps5_startup(void)
{
   platform_init(PS5_TITLE_NAME);
   mkdir("/app0/user", 0777);
   allow_directory_access("/app0/user");
   mkdir("/app0/user/cache", 0777);
   allow_directory_access("/app0/user/cache");
   seed_shader_caches();
   repair_cache_directories("/app0/radv-shader-cache");
   /* SDL_GetPrefPath is $XDG_DATA_HOME/<org>/<app>/: saves and settings. A title
    * sees its own folder (/app0, which is /data/homebrew/<TITLE_ID>) and nothing
    * else of /data, so they live there. Set in SDL's own copy of the environment,
    * which is all SDL_getenv reads: the console's libc keeps what setenv sets
    * apart from the `environ` SDL copies. */
   SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "XDG_DATA_HOME", "/app0/userdata", true);
   SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "HOME", "/app0/userdata", true);
}
