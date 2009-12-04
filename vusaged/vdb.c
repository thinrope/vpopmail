/*
   $Id$
*/

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#ifdef ASSERT_DEBUG
   #include <assert.h>
#endif
#include <storage.h>
#include <conf.h>
#include "path.h"
#include "user.h"
#include "domain.h"
#include "userstore.h"
#include "directory.h"
#include "vdb.h"

extern user_t *userlist;
extern domain_t *domainlist;
extern storage_t userlist_num, domainlist_num;

static int vdb_fd = -1;
static char *vdb_database = NULL;

static inline int vdb_write(void *, size_t);
static inline int vdb_close(void);
static inline int vdb_remove(void);

/*
   Read vusage database configuration
*/

int vdb_init(config_t *config)
{
   char *str = NULL;

   vdb_database = NULL;

   if (config == NULL)
	  return 0;

   str = config_fetch_by_name(config, "Storage", "Filename");
   if (str == NULL) {
	  fprintf(stderr, "vdb_init: not saving database\n");
	  return 1;
   }

   if (!(*str)) {
	  fprintf(stderr, "vdb_init: syntax error: Storage::Filename\n");
	  return 0;
   }

   vdb_database = strdup(str);
   if (vdb_database == NULL) {
	  fprintf(stderr, "vdb_init: strdup failed\n");
	  return 0;
   }

   return 1;
}

/*
   Save vusage database
*/

int vdb_save(void)
{
   int ret = 0, len = 0, l_int = 0, i = 0;
   vdb_header_t header;
   domain_t *d = NULL;
   user_t *u = NULL;
   storage_t num = 0, l_storage = 0;
   struct stat l_stat;
   time_t l_time = 0;
   char l_domain[DOMAIN_MAX_DOMAIN] = { 0 }, l_user[USER_MAX_USERNAME] = { 0 },
		l_path[PATH_MAX] = { 0 };

   if (vdb_database == NULL)
	  return 1;

   /*
	  Truncate storage file
   */

   vdb_fd = open(vdb_database, O_WRONLY|O_CREAT|O_TRUNC, 0600);
   if (vdb_fd == -1) {
	  fprintf(stderr, "vdb_save: open(%s) failed: %d\n", vdb_database, errno);
	  return 0;
   }

   /*
	  Fill header
   */

   memset(&header, 0, sizeof(header));

   header.version = 0x02;
   memcpy(header.id, VDB_HEADER_ID, 3);
   header.num_domains = domainlist_num;
   header.num_users = userlist_num;

   ret = vdb_write(&header, sizeof(header));
   if (!ret)
	  return 0;

   /*
	  Fix values
   */

   header.num_domains = domainlist_num;
   header.num_users = userlist_num;

   /*
	  Write database
   */

   /*
	  Write domains
   */

   num = 0;

   for (d = domainlist; d; d = d->next) {
#ifdef ASSERT_DEBUG
	  assert(d->domain != NULL);
	  assert(*(d->domain) != '\0');
#endif

	  len = strlen(d->domain);
	  if (len >= sizeof(l_domain)) {
		 fprintf(stderr, "vdb_save: domain name too long: %s\n", d->domain);
		 vdb_remove();
		 return 0;
	  }

	  memset(l_domain, 0, sizeof(l_domain));
	  memcpy(l_domain, d->domain, len);

	  ret = vdb_write(l_domain, sizeof(l_domain));
	  if (!ret)
		 return 0;

	  ret = vdb_write(&d->usage, sizeof(storage_t));
	  if (!ret)
		 return 0;

	  ret = vdb_write(&d->count, sizeof(storage_t));
	  if (!ret)
		 return 0;

	  num++;
   }

#ifdef ASSERT_DEBUG
   assert(num == domainlist_num);
#endif

   /*
	  Write users and directories
   */

   num = 0;

   for (u = userlist; u; u = u->next) {
#ifdef ASSERT_DEBUG
	  assert(u->user != NULL);
	  assert(*(u->user));
	  assert(u->home != NULL);
	  assert(*(u->home));
	  assert(u->domain != NULL);
	  assert(u->domain->domain != NULL);
	  assert(*(u->domain->domain));
#endif

	  /*
		 username
	  */

	  len = strlen(u->user);
	  if (len >= sizeof(l_user)) {
		 fprintf(stderr, "vdb_save: username too long: %s\n", u->user);
		 vdb_remove();
		 return 0;
	  }

	  memset(l_user, 0, sizeof(l_user));
	  memcpy(l_user, u->user, len);

	  ret = vdb_write(l_user, sizeof(l_user));
	  if (!ret)
		 return 0;

	  /*
		 domain
	  */

	  len = strlen(u->domain->domain);
	  if (len >= sizeof(l_domain)) {
		 fprintf(stderr, "vdb_save: domain too long: %s\n", u->domain->domain);
		 vdb_remove();
		 return 0;
	  }

	  memset(l_domain, 0, sizeof(l_domain));
	  memcpy(l_domain, u->domain->domain, len);

	  ret = vdb_write(l_domain, sizeof(l_domain));
	  if (!ret)
		 return 0;

	  /*
		 home directory
	  */

	  len = strlen(u->home);
	  if (len >= sizeof(l_path)) {
		 fprintf(stderr, "vdb_save: path too long: %s\n", u->home);
		 vdb_remove();
		 return 0;
	  }

	  memset(l_path, 0, sizeof(l_path));
	  memcpy(l_path, u->home, len);

	  ret = vdb_write(l_path, sizeof(l_path));
	  if (!ret)
		 return 0;

	  /*
		 userstore:stat
	  */

	  memset(&l_stat, 0, sizeof(l_stat));

	  if (u->userstore)
		 memcpy(&l_stat, &u->userstore->st, sizeof(struct stat));

	  ret = vdb_write(&l_stat, sizeof(l_stat));
	  if (!ret)
		 return 0;

	  /*
		 userstore:last_updated
	  */

	  l_time = 0;
	  if (u->userstore)
		 l_time = u->userstore->last_updated;

	  ret = vdb_write(&l_time, sizeof(l_time));
	  if (!ret)
		 return 0;

	  /*
		 userstore:time_taken
	  */

	  l_time = 0;
	  if (u->userstore)
		 l_time = u->userstore->time_taken;

	  ret = vdb_write(&l_time, sizeof(l_time));
	  if (!ret)
		 return 0;

	  /*
		 userstore:lastauth
	  */

	  l_time = 0;
	  if (u->userstore)
		 l_time = u->userstore->lastauth;

	  ret = vdb_write(&l_time, sizeof(l_time));
	  if (!ret)
		 return 0;

	  /*
		 userstore:usage
	  */

	  l_storage = 0;
	  if (u->userstore)
		 l_storage = u->userstore->usage;

	  ret = vdb_write(&l_storage, sizeof(l_storage));
	  if (!ret)
		 return 0;

	  /*
		 userstore:count
	  */

	  l_storage = 0;
	  if (u->userstore)
		 l_storage = u->userstore->count;

	  ret = vdb_write(&l_storage, sizeof(l_storage));
	  if (!ret)
		 return 0;

	  /*
		 userstore:num_directories
	  */
   
	  l_int = 0;
	  if (u->userstore)
		 l_int = u->userstore->num_directories;

	  ret = vdb_write(&l_int, sizeof(l_int));
	  if (!ret)
		 return 0;

	  /*
		 userstore:directories
	  */

	  for (i = 0; ((u->userstore) && (i < l_int)); i++) {
#ifdef ASSERT_DEBUG
		 assert(u->userstore->directory != NULL);
		 assert(u->userstore->directory[i] != NULL);
		 assert(u->userstore->directory[i]->directory != NULL);
		 assert(*(u->userstore->directory[i]->directory) != '\0');
#endif

		 /*
			directory:directory
		 */

		 len = strlen(u->userstore->directory[i]->directory);
		 if (len >= sizeof(l_path)) {
			fprintf(stderr, "vdb_write: path too long: %s\n", u->userstore->directory[i]->directory);
			vdb_remove();
			return 0;
		 }

		 memset(l_path, 0, sizeof(l_path));
		 memcpy(l_path, u->userstore->directory[i]->directory, len);

		 ret = vdb_write(l_path, sizeof(l_path));
		 if (!ret)
			return 0;

		 /*
			directory:last_update
		 */

		 ret = vdb_write(&u->userstore->directory[i]->last_update, sizeof(l_time));
		 if (!ret)
			return 0;

		 /*
			directory:stat
		 */

		 ret = vdb_write(&u->userstore->directory[i]->st, sizeof(struct stat));
		 if (!ret)
			return 0;

		 /*
			directory:usage
		 */

		 ret = vdb_write(&u->userstore->directory[i]->usage, sizeof(storage_t));
		 if (!ret)
			return 0;

		 /*
			directory:count
		 */

		 ret = vdb_write(&u->userstore->directory[i]->count, sizeof(storage_t));
		 if (!ret)
			return 0;
	  }
   }

   vdb_close();
   return 1;
}

/*
   Write value to vdb descriptor
   If a failure occurs, back out and print error
*/

static inline int vdb_write(void *data, size_t len)
{
   ssize_t wret = 0;

#ifdef ASSERT_DEBUG
   assert(vdb_fd != -1);
#endif

   wret = write(vdb_fd, data, len);
   if (wret != len) {
	  vdb_remove();
	  printf("vdb_write: write failed: %d (%d)\n", wret, errno);
	  return 0;
   }

   return 1;
}

/*
   Safely close database
*/

static inline int vdb_close(void)
{
   if (vdb_fd == -1)
	  return 1;

   close(vdb_fd);
   vdb_fd = -1;

   return 1;
}

/*
   Remove database file
*/

static inline int vdb_remove(void)
{
   if (vdb_database == NULL)
	  return 1;

   unlink(vdb_database);
   vdb_close();

   return 1;
}
