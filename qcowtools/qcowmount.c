/*
 * Mounts a QEMU Copy-On-Write (QCOW) image file
 *
 * Copyright (C) 2010-2017, Joachim Metz <joachim.metz@gmail.com>
 *
 * Refer to AUTHORS for acknowledgements.
 *
 * This software is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this software.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <common.h>
#include <file_stream.h>
#include <memory.h>
#include <narrow_string.h>
#include <system_string.h>
#include <types.h>
#include <wide_string.h>

#if defined( HAVE_ERRNO_H )
#include <errno.h>
#endif

#if defined( HAVE_UNISTD_H )
#include <unistd.h>
#endif

#if defined( HAVE_STDLIB_H ) || defined( WINAPI )
#include <stdlib.h>
#endif

#if !defined( WINAPI ) || defined( USE_CRT_FUNCTIONS )
#if defined( TIME_WITH_SYS_TIME )
#include <sys/time.h>
#include <time.h>
#elif defined( HAVE_SYS_TIME_H )
#include <sys/time.h>
#else
#include <time.h>
#endif
#endif

#if defined( HAVE_LIBFUSE ) || defined( HAVE_LIBOSXFUSE )
#define FUSE_USE_VERSION	26

#if defined( HAVE_LIBFUSE )
#include <fuse.h>

#elif defined( HAVE_LIBOSXFUSE )
#include <osxfuse/fuse.h>
#endif

#elif defined( HAVE_LIBDOKAN )
#include <dokan.h>
#endif

#include "mount_handle.h"
#include "qcowtools_getopt.h"
#include "qcowtools_libcerror.h"
#include "qcowtools_libclocale.h"
#include "qcowtools_libcnotify.h"
#include "qcowtools_libqcow.h"
#include "qcowtools_output.h"
#include "qcowtools_signal.h"
#include "qcowtools_unused.h"

mount_handle_t *qcowmount_mount_handle = NULL;
int qcowmount_abort                    = 0;

/* Prints the executable usage information
 */
void usage_fprint(
      FILE *stream )
{
	if( stream == NULL )
	{
		return;
	}
	fprintf( stream, "Use qcowmount to mount the QEMU Copy-On-Write (QCOW)\n"
                         "image file\n\n" );

	fprintf( stream, "Usage: qcowmount [ -k keys ] [ -p password ]\n"
	                 "                 [ -X extended_options ] [ -hvV ]\n"
	                 "                 qcow_file mount_point\n\n" );

	fprintf( stream, "\tqcow_file:   the QCOW image file\n\n" );
	fprintf( stream, "\tmount_point: the directory to serve as mount point\n\n" );

	fprintf( stream, "\t-h:          shows this help\n" );
	fprintf( stream, "\t-k:          the key formatted in base16\n" );
	fprintf( stream, "\t-p:          specify the password/passphrase\n" );
	fprintf( stream, "\t-v:          verbose output to stderr\n"
	                 "\t             qcowmount will remain running in the foreground\n" );
	fprintf( stream, "\t-V:          print version\n" );
	fprintf( stream, "\t-X:          extended options to pass to sub system\n" );
}

/* Signal handler for qcowmount
 */
void qcowmount_signal_handler(
      qcowtools_signal_t signal QCOWTOOLS_ATTRIBUTE_UNUSED )
{
	libcerror_error_t *error = NULL;
	static char *function    = "qcowmount_signal_handler";

	QCOWTOOLS_UNREFERENCED_PARAMETER( signal )

	qcowmount_abort = 1;

	if( qcowmount_mount_handle != NULL )
	{
		if( mount_handle_signal_abort(
		     qcowmount_mount_handle,
		     &error ) != 1 )
		{
			libcnotify_printf(
			 "%s: unable to signal mount handle to abort.\n",
			 function );

			libcnotify_print_error_backtrace(
			 error );
			libcerror_error_free(
			 &error );
		}
	}
	/* Force stdin to close otherwise any function reading it will remain blocked
	 */
#if defined( WINAPI ) && !defined( __CYGWIN__ )
	if( _close(
	     0 ) != 0 )
#else
	if( close(
	     0 ) != 0 )
#endif
	{
		libcnotify_printf(
		 "%s: unable to close stdin.\n",
		 function );
	}
}

#if defined( HAVE_LIBFUSE ) || defined( HAVE_LIBOSXFUSE )

#if ( SIZEOF_OFF_T != 8 ) && ( SIZEOF_OFF_T != 4 )
#error Size of off_t not supported
#endif

static char *qcowmount_fuse_path_prefix         = "/qcow";
static size_t qcowmount_fuse_path_prefix_length = 5;

#if defined( HAVE_TIME )
time_t qcowmount_timestamp                      = 0;
#endif

/* Opens a file or directory
 * Returns 0 if successful or a negative errno value otherwise
 */
int qcowmount_fuse_open(
     const char *path,
     struct fuse_file_info *file_info )
{
	libcerror_error_t *error = NULL;
	static char *function    = "qcowmount_fuse_open";
	size_t path_length       = 0;
	int result               = 0;

	if( path == NULL )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_ARGUMENTS,
		 LIBCERROR_ARGUMENT_ERROR_INVALID_VALUE,
		 "%s: invalid path.",
		 function );

		result = -EINVAL;

		goto on_error;
	}
	if( file_info == NULL )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_ARGUMENTS,
		 LIBCERROR_ARGUMENT_ERROR_INVALID_VALUE,
		 "%s: invalid file info.",
		 function );

		result = -EINVAL;

		goto on_error;
	}
	path_length = narrow_string_length(
	               path );

	if( ( path_length <= qcowmount_fuse_path_prefix_length )
         || ( path_length > ( qcowmount_fuse_path_prefix_length + 3 ) )
	 || ( narrow_string_compare(
	       path,
	       qcowmount_fuse_path_prefix,
	       qcowmount_fuse_path_prefix_length ) != 0 ) )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_ARGUMENTS,
		 LIBCERROR_ARGUMENT_ERROR_UNSUPPORTED_VALUE,
		 "%s: unsupported path: %s.",
		 function,
		 path );

		result = -ENOENT;

		goto on_error;
	}
	if( ( file_info->flags & 0x03 ) != O_RDONLY )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_RUNTIME,
		 LIBCERROR_RUNTIME_ERROR_UNSUPPORTED_VALUE,
		 "%s: write access currently not supported.",
		 function );

		result = -EACCES;

		goto on_error;
	}
	return( 0 );

on_error:
	if( error != NULL )
	{
		libcnotify_print_error_backtrace(
		 error );
		libcerror_error_free(
		 &error );
	}
	return( result );
}

/* Reads a buffer of data at the specified offset
 * Returns number of bytes read if successful or a negative errno value otherwise
 */
int qcowmount_fuse_read(
     const char *path,
     char *buffer,
     size_t size,
     off_t offset,
     struct fuse_file_info *file_info )
{
	libcerror_error_t *error = NULL;
	static char *function    = "qcowmount_fuse_read";
	size_t path_length       = 0;
	ssize_t read_count       = 0;
	int input_file_index     = 0;
	int result               = 0;
	int string_index         = 0;

	if( path == NULL )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_ARGUMENTS,
		 LIBCERROR_ARGUMENT_ERROR_INVALID_VALUE,
		 "%s: invalid path.",
		 function );

		result = -EINVAL;

		goto on_error;
	}
	if( size > (size_t) INT_MAX )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_ARGUMENTS,
		 LIBCERROR_ARGUMENT_ERROR_VALUE_EXCEEDS_MAXIMUM,
		 "%s: invalid size value exceeds maximum.",
		 function );

		result = -EINVAL;

		goto on_error;
	}
	if( file_info == NULL )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_ARGUMENTS,
		 LIBCERROR_ARGUMENT_ERROR_INVALID_VALUE,
		 "%s: invalid file info.",
		 function );

		result = -EINVAL;

		goto on_error;
	}
	path_length = narrow_string_length(
	               path );

	if( ( path_length <= qcowmount_fuse_path_prefix_length )
         || ( path_length > ( qcowmount_fuse_path_prefix_length + 3 ) )
	 || ( narrow_string_compare(
	       path,
	       qcowmount_fuse_path_prefix,
	       qcowmount_fuse_path_prefix_length ) != 0 ) )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_ARGUMENTS,
		 LIBCERROR_ARGUMENT_ERROR_UNSUPPORTED_VALUE,
		 "%s: unsupported path: %s.",
		 function,
		 path );

		result = -ENOENT;

		goto on_error;
	}
	string_index = (int) qcowmount_fuse_path_prefix_length;

	input_file_index = path[ string_index++ ] - '0';

	if( string_index < (int) path_length )
	{
		input_file_index *= 10;
		input_file_index += path[ string_index++ ] - '0';
	}
	if( string_index < (int) path_length )
	{
		input_file_index *= 10;
		input_file_index += path[ string_index++ ] - '0';
	}
	input_file_index -= 1;

	if( mount_handle_seek_offset(
	     qcowmount_mount_handle,
	     input_file_index,
	     (off64_t) offset,
	     SEEK_SET,
	     &error ) == -1 )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_IO,
		 LIBCERROR_IO_ERROR_SEEK_FAILED,
		 "%s: unable to seek offset in mount handle.",
		 function );

		result = -EIO;

		goto on_error;
	}
	read_count = mount_handle_read_buffer(
	              qcowmount_mount_handle,
	              input_file_index,
	              (uint8_t *) buffer,
	              size,
	              &error );

	if( read_count == -1 )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_IO,
		 LIBCERROR_IO_ERROR_READ_FAILED,
		 "%s: unable to read from mount handle.",
		 function );

		result = -EIO;

		goto on_error;
	}
	return( (int) read_count );

on_error:
	if( error != NULL )
	{
		libcnotify_print_error_backtrace(
		 error );
		libcerror_error_free(
		 &error );
	}
	return( result );
}

/* Sets the values in a stat info structure
 * Returns 1 if successful or -1 on error
 */
int qcowmount_fuse_set_stat_info(
     struct stat *stat_info,
     size64_t size,
     int number_of_sub_items,
     uint8_t use_mount_time,
     libcerror_error_t **error )
{
	static char *function = "qcowmount_fuse_set_stat_info";

	if( stat_info == NULL )
	{
		libcerror_error_set(
		 error,
		 LIBCERROR_ERROR_DOMAIN_ARGUMENTS,
		 LIBCERROR_ARGUMENT_ERROR_INVALID_VALUE,
		 "%s: invalid stat info.",
		 function );

		return( -1 );
	}
#if defined( HAVE_TIME )
	if( use_mount_time != 0 )
	{
		if( qcowmount_timestamp == 0 )
		{
			if( time(
			     &qcowmount_timestamp ) == (time_t) -1 )
			{
				qcowmount_timestamp = 0;
			}
		}
		stat_info->st_atime = qcowmount_timestamp;
		stat_info->st_mtime = qcowmount_timestamp;
		stat_info->st_ctime = qcowmount_timestamp;
	}
#endif
	if( size != 0 )
	{
#if SIZEOF_OFF_T <= 4
		if( size > (size64_t) UINT32_MAX )
#else
		if( size > (size64_t) INT64_MAX )
#endif
		{
			libcerror_error_set(
			 error,
			 LIBCERROR_ERROR_DOMAIN_RUNTIME,
			 LIBCERROR_RUNTIME_ERROR_VALUE_OUT_OF_BOUNDS,
			 "%s: invalid size value out of bounds.",
			 function );

			return( -1 );
		}
		stat_info->st_size = (off_t) size;
	}
	if( number_of_sub_items > 0 )
	{
		stat_info->st_mode  = S_IFDIR | 0555;
		stat_info->st_nlink = 2;
	}
	else
	{
		stat_info->st_mode  = S_IFREG | 0444;
		stat_info->st_nlink = 1;
	}
#if defined( HAVE_GETEUID )
	stat_info->st_uid = geteuid();
#endif
#if defined( HAVE_GETEGID )
	stat_info->st_gid = getegid();
#endif
	return( 1 );
}

/* Fills a directory entry
 * Returns 1 if successful or -1 on error
 */
int qcowmount_fuse_filldir(
     void *buffer,
     fuse_fill_dir_t filler,
     char *name,
     size_t name_size,
     struct stat *stat_info,
     mount_handle_t *mount_handle,
     int input_file_index,
     uint8_t use_mount_time,
     libcerror_error_t **error )
{
	static char *function   = "qcowmount_fuse_filldir";
	size64_t media_size     = 0;
	int number_of_sub_items = 0;

	if( filler == NULL )
	{
		libcerror_error_set(
		 error,
		 LIBCERROR_ERROR_DOMAIN_ARGUMENTS,
		 LIBCERROR_ARGUMENT_ERROR_INVALID_VALUE,
		 "%s: invalid filler.",
		 function );

		return( -1 );
	}
	if( mount_handle == NULL )
	{
		number_of_sub_items = 1;
	}
	else
	{
		if( mount_handle_get_media_size(
		     mount_handle,
		     input_file_index,
		     &media_size,
		     error ) != 1 )
		{
			libcerror_error_set(
			 error,
			 LIBCERROR_ERROR_DOMAIN_RUNTIME,
			 LIBCERROR_RUNTIME_ERROR_GET_FAILED,
			 "%s: unable to retrieve media size.",
			 function );

			return( -1 );
		}
	}
	if( memory_set(
	     stat_info,
	     0,
	     sizeof( struct stat ) ) == NULL )
	{
		libcerror_error_set(
		 error,
		 LIBCERROR_ERROR_DOMAIN_MEMORY,
		 LIBCERROR_MEMORY_ERROR_SET_FAILED,
		 "%s: unable to clear stat info.",
		 function );

		return( -1 );
	}
	if( qcowmount_fuse_set_stat_info(
	     stat_info,
	     media_size,
	     number_of_sub_items,
	     use_mount_time,
	     error ) != 1 )
	{
		libcerror_error_set(
		 error,
		 LIBCERROR_ERROR_DOMAIN_RUNTIME,
		 LIBCERROR_RUNTIME_ERROR_SET_FAILED,
		 "%s: unable to set stat info.",
		 function );

		return( -1 );
	}
	if( filler(
	     buffer,
	     name,
	     stat_info,
	     0 ) == 1 )
	{
		libcerror_error_set(
		 error,
		 LIBCERROR_ERROR_DOMAIN_RUNTIME,
		 LIBCERROR_RUNTIME_ERROR_SET_FAILED,
		 "%s: unable to set directory entry.",
		 function );

		return( -1 );
	}
	return( 1 );
}

/* Reads a directory
 * Returns 0 if successful or a negative errno value otherwise
 */
int qcowmount_fuse_readdir(
     const char *path,
     void *buffer,
     fuse_fill_dir_t filler,
     off_t offset QCOWTOOLS_ATTRIBUTE_UNUSED,
     struct fuse_file_info *file_info QCOWTOOLS_ATTRIBUTE_UNUSED )
{
	char qcowmount_fuse_path[ 10 ];

	libcerror_error_t *error  = NULL;
	struct stat *stat_info    = NULL;
	static char *function     = "qcowmount_fuse_readdir";
	size_t path_length        = 0;
	int input_file_index      = 0;
	int number_of_input_files = 0;
	int result                = 0;
	int string_index          = 0;

	QCOWTOOLS_UNREFERENCED_PARAMETER( offset )
	QCOWTOOLS_UNREFERENCED_PARAMETER( file_info )

	if( path == NULL )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_ARGUMENTS,
		 LIBCERROR_ARGUMENT_ERROR_INVALID_VALUE,
		 "%s: invalid path.",
		 function );

		result = -EINVAL;

		goto on_error;
	}
	path_length = narrow_string_length(
	               path );

	if( ( path_length != 1 )
	 || ( path[ 0 ] != '/' ) )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_ARGUMENTS,
		 LIBCERROR_ARGUMENT_ERROR_UNSUPPORTED_VALUE,
		 "%s: unsupported path: %s.",
		 function,
		 path );

		result = -ENOENT;

		goto on_error;
	}
	if( narrow_string_copy(
	     qcowmount_fuse_path,
	     qcowmount_fuse_path_prefix,
	     qcowmount_fuse_path_prefix_length ) == NULL )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_MEMORY,
		 LIBCERROR_MEMORY_ERROR_COPY_FAILED,
		 "%s: unable to copy path prefix.",
		 function );

		result = -errno;

		goto on_error;
	}
	if( mount_handle_get_number_of_input_files(
	     qcowmount_mount_handle,
	     &number_of_input_files,
	     &error ) != 1 )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_RUNTIME,
		 LIBCERROR_RUNTIME_ERROR_GET_FAILED,
		 "%s: unable to retrieve number of input files.",
		 function );

		result = -EIO;

		goto on_error;
	}
	if( ( number_of_input_files < 0 )
	 || ( number_of_input_files > 99 ) )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_ARGUMENTS,
		 LIBCERROR_ARGUMENT_ERROR_UNSUPPORTED_VALUE,
		 "%s: unsupported number of input files.",
		 function );

		result = -ENOENT;

		goto on_error;
	}
	stat_info = memory_allocate_structure(
	             struct stat );

	if( stat_info == NULL )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_MEMORY,
		 LIBCERROR_MEMORY_ERROR_INSUFFICIENT,
		 "%s: unable to create stat info.",
		 function );

		result = errno;

		goto on_error;
	}
	if( qcowmount_fuse_filldir(
	     buffer,
	     filler,
	     ".",
	     2,
	     stat_info,
	     NULL,
	     -1,
	     1,
	     &error ) != 1 )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_RUNTIME,
		 LIBCERROR_RUNTIME_ERROR_SET_FAILED,
		 "%s: unable to set directory entry.",
		 function );

		result = -EIO;

		goto on_error;
	}
	if( qcowmount_fuse_filldir(
	     buffer,
	     filler,
	     "..",
	     3,
	     stat_info,
	     NULL,
	     -1,
	     0,
	     &error ) != 1 )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_RUNTIME,
		 LIBCERROR_RUNTIME_ERROR_SET_FAILED,
		 "%s: unable to set directory entry.",
		 function );

		result = -EIO;

		goto on_error;
	}
	for( input_file_index = 1;
	     input_file_index <= number_of_input_files;
	     input_file_index++ )
	{
		string_index = qcowmount_fuse_path_prefix_length;

		if( input_file_index >= 100 )
		{
			qcowmount_fuse_path[ string_index++ ] = '0' + (char) ( input_file_index / 100 );
		}
		if( input_file_index >= 10 )
		{
			qcowmount_fuse_path[ string_index++ ] = '0' + (char) ( input_file_index / 10 );
		}
		qcowmount_fuse_path[ string_index++ ] = '0' + (char) ( input_file_index % 10 );
		qcowmount_fuse_path[ string_index++ ] = 0;

/* TODO add support for multiple input files ? */
		if( input_file_index != 1 )
		{
			libcerror_error_set(
			 &error,
			 LIBCERROR_ERROR_DOMAIN_RUNTIME,
			 LIBCERROR_RUNTIME_ERROR_VALUE_OUT_OF_BOUNDS,
			 "%s: invalid input file index value out of bounds.",
			 function );

			result = -EIO;

			goto on_error;
		}
		if( qcowmount_fuse_filldir(
		     buffer,
		     filler,
		     &( qcowmount_fuse_path[ 1 ] ),
		     string_index - 1,
		     stat_info,
		     qcowmount_mount_handle,
		     input_file_index - 1,
		     1,
		     &error ) != 1 )
		{
			libcerror_error_set(
			 &error,
			 LIBCERROR_ERROR_DOMAIN_RUNTIME,
			 LIBCERROR_RUNTIME_ERROR_SET_FAILED,
			 "%s: unable to set directory entry.",
			 function );

			result = -EIO;

			goto on_error;
		}
	}
	memory_free(
	 stat_info );

	return( 0 );

on_error:
	if( error != NULL )
	{
		libcnotify_print_error_backtrace(
		 error );
		libcerror_error_free(
		 &error );
	}
	if( stat_info != NULL )
	{
		memory_free(
		 stat_info );
	}
	return( result );
}

/* Retrieves the file stat info
 * Returns 0 if successful or a negative errno value otherwise
 */
int qcowmount_fuse_getattr(
     const char *path,
     struct stat *stat_info )
{
	libcerror_error_t *error = NULL;
	static char *function    = "qcowmount_fuse_getattr";
	size64_t media_size      = 0;
	size_t path_length       = 0;
	int input_file_index     = 0;
	int number_of_sub_items  = 0;
	int result               = -ENOENT;
	int string_index         = 0;
	uint8_t use_mount_time   = 0;

	if( path == NULL )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_ARGUMENTS,
		 LIBCERROR_ARGUMENT_ERROR_INVALID_VALUE,
		 "%s: invalid path.",
		 function );

		result = -EINVAL;

		goto on_error;
	}
	if( stat_info == NULL )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_ARGUMENTS,
		 LIBCERROR_ARGUMENT_ERROR_INVALID_VALUE,
		 "%s: invalid stat info.",
		 function );

		result = -EINVAL;

		goto on_error;
	}
	if( memory_set(
	     stat_info,
	     0,
	     sizeof( struct stat ) ) == NULL )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_MEMORY,
		 LIBCERROR_MEMORY_ERROR_SET_FAILED,
		 "%s: unable to clear stat info.",
		 function );

		result = errno;

		goto on_error;
	}
	path_length = narrow_string_length(
	               path );

	if( path_length == 1 )
	{
		if( path[ 0 ] == '/' )
		{
			number_of_sub_items = 1;
			use_mount_time      = 1;
			result              = 0;
		}
	}
	else if( ( path_length > qcowmount_fuse_path_prefix_length )
	      && ( path_length <= ( qcowmount_fuse_path_prefix_length + 3 ) ) )
	{
		if( narrow_string_compare(
		     path,
		     qcowmount_fuse_path_prefix,
		     qcowmount_fuse_path_prefix_length ) == 0 )
		{
			string_index = qcowmount_fuse_path_prefix_length;

			input_file_index = path[ string_index++ ] - '0';

			if( string_index < (int) path_length )
			{
				input_file_index *= 10;
				input_file_index += path[ string_index++ ] - '0';
			}
			if( string_index < (int) path_length )
			{
				input_file_index *= 10;
				input_file_index += path[ string_index++ ] - '0';
			}
			input_file_index -= 1;

/* TODO add support for multiple input files ? */
			if( input_file_index != 0 )
			{
				libcerror_error_set(
				 &error,
				 LIBCERROR_ERROR_DOMAIN_RUNTIME,
				 LIBCERROR_RUNTIME_ERROR_VALUE_OUT_OF_BOUNDS,
				 "%s: invalid input file index value out of bounds.",
				 function );

				result = -ERANGE;

				goto on_error;
			}
			if( mount_handle_get_media_size(
			     qcowmount_mount_handle,
			     input_file_index,
			     &media_size,
			     &error ) != 1 )
			{
				libcerror_error_set(
				 &error,
				 LIBCERROR_ERROR_DOMAIN_RUNTIME,
				 LIBCERROR_RUNTIME_ERROR_GET_FAILED,
				 "%s: unable to retrieve media size.",
				 function );

				result = -EIO;

				goto on_error;
			}
			use_mount_time = 1;
			result         = 0;
		}
	}
	if( result == 0 )
	{
		if( qcowmount_fuse_set_stat_info(
		     stat_info,
		     media_size,
		     number_of_sub_items,
		     use_mount_time,
		     &error ) != 1 )
		{
			libcerror_error_set(
			 &error,
			 LIBCERROR_ERROR_DOMAIN_RUNTIME,
			 LIBCERROR_RUNTIME_ERROR_SET_FAILED,
			 "%s: unable to set stat info.",
			 function );

			result = -EIO;

			goto on_error;
		}
	}
	return( result );

on_error:
	if( error != NULL )
	{
		libcnotify_print_error_backtrace(
		 error );
		libcerror_error_free(
		 &error );
	}
	return( result );
}

/* Cleans up when fuse is done
 */
void qcowmount_fuse_destroy(
      void *private_data QCOWTOOLS_ATTRIBUTE_UNUSED )
{
	libcerror_error_t *error = NULL;
	static char *function    = "qcowmount_fuse_destroy";

	QCOWTOOLS_UNREFERENCED_PARAMETER( private_data )

	if( qcowmount_mount_handle != NULL )
	{
		if( mount_handle_free(
		     &qcowmount_mount_handle,
		     &error ) != 1 )
		{
			libcerror_error_set(
			 &error,
			 LIBCERROR_ERROR_DOMAIN_RUNTIME,
			 LIBCERROR_RUNTIME_ERROR_FINALIZE_FAILED,
			 "%s: unable to free mount handle.",
			 function );

			goto on_error;
		}
	}
	return;

on_error:
	if( error != NULL )
	{
		libcnotify_print_error_backtrace(
		 error );
		libcerror_error_free(
		 &error );
	}
	return;
}

#elif defined( HAVE_LIBDOKAN )

static wchar_t *qcowmount_dokan_path_prefix      = L"\\QCOW";
static size_t qcowmount_dokan_path_prefix_length = 5;

/* Opens a file or directory
 * Returns 0 if successful or a negative error code otherwise
 */
int __stdcall qcowmount_dokan_CreateFile(
               const wchar_t *path,
               DWORD desired_access,
               DWORD share_mode QCOWTOOLS_ATTRIBUTE_UNUSED,
               DWORD creation_disposition,
               DWORD attribute_flags QCOWTOOLS_ATTRIBUTE_UNUSED,
               DOKAN_FILE_INFO *file_info )
{
	libcerror_error_t *error = NULL;
	static char *function    = "qcowmount_dokan_CreateFile";
	size_t path_length       = 0;
	int result               = 0;

	QCOWTOOLS_UNREFERENCED_PARAMETER( share_mode )
	QCOWTOOLS_UNREFERENCED_PARAMETER( attribute_flags )

	if( path == NULL )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_ARGUMENTS,
		 LIBCERROR_ARGUMENT_ERROR_INVALID_VALUE,
		 "%s: invalid path.",
		 function );

		result = -ERROR_BAD_ARGUMENTS;

		goto on_error;
	}
	if( ( desired_access & GENERIC_WRITE ) != 0 )
	{
		return( -ERROR_WRITE_PROTECT );
	}
	/* Ignore the share_mode
	 */
	if( creation_disposition == CREATE_NEW )
	{
		return( -ERROR_FILE_EXISTS );
	}
	else if( creation_disposition == CREATE_ALWAYS )
	{
		return( -ERROR_ALREADY_EXISTS );
	}
	else if( creation_disposition == OPEN_ALWAYS )
	{
		return( -ERROR_FILE_NOT_FOUND );
	}
	else if( creation_disposition == TRUNCATE_EXISTING )
	{
		return( -ERROR_FILE_NOT_FOUND );
	}
	else if( creation_disposition != OPEN_EXISTING )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_ARGUMENTS,
		 LIBCERROR_ARGUMENT_ERROR_INVALID_VALUE,
		 "%s: invalid creation disposition.",
		 function );

		result = -ERROR_BAD_ARGUMENTS;

		goto on_error;
	}
	if( file_info == NULL )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_ARGUMENTS,
		 LIBCERROR_ARGUMENT_ERROR_INVALID_VALUE,
		 "%s: invalid file info.",
		 function );

		result = -ERROR_BAD_ARGUMENTS;

		goto on_error;
	}
	path_length = wide_string_length(
	               path );

	if( path_length == 1 )
	{
		if( path[ 0 ] != (wchar_t) '\\' )
		{
			libcerror_error_set(
			 &error,
			 LIBCERROR_ERROR_DOMAIN_ARGUMENTS,
			 LIBCERROR_ARGUMENT_ERROR_UNSUPPORTED_VALUE,
			 "%s: unsupported path: %ls.",
			 function,
			 path );

			result = -ERROR_FILE_NOT_FOUND;

			goto on_error;
		}
	}
	else
	{
		if( ( path_length <= qcowmount_dokan_path_prefix_length )
		 || ( path_length > ( qcowmount_dokan_path_prefix_length + 3 ) )
		 || ( wide_string_compare(
		       path,
		       qcowmount_dokan_path_prefix,
		       qcowmount_dokan_path_prefix_length ) != 0 ) )
		{
			libcerror_error_set(
			 &error,
			 LIBCERROR_ERROR_DOMAIN_ARGUMENTS,
			 LIBCERROR_ARGUMENT_ERROR_UNSUPPORTED_VALUE,
			 "%s: unsupported path: %ls.",
			 function,
			 path );

			result = -ERROR_FILE_NOT_FOUND;

			goto on_error;
		}
	}
	return( 0 );

on_error:
	if( error != NULL )
	{
		libcnotify_print_error_backtrace(
		 error );
		libcerror_error_free(
		 &error );
	}
	return( result );
}

/* Opens a directory
 * Returns 0 if successful or a negative error code otherwise
 */
int __stdcall qcowmount_dokan_OpenDirectory(
               const wchar_t *path,
               DOKAN_FILE_INFO *file_info QCOWTOOLS_ATTRIBUTE_UNUSED )
{
	libcerror_error_t *error = NULL;
	static char *function    = "qcowmount_dokan_OpenDirectory";
	size_t path_length       = 0;
	int result               = 0;

	QCOWTOOLS_UNREFERENCED_PARAMETER( file_info )

	if( path == NULL )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_ARGUMENTS,
		 LIBCERROR_ARGUMENT_ERROR_INVALID_VALUE,
		 "%s: invalid path.",
		 function );

		result = -ERROR_BAD_ARGUMENTS;

		goto on_error;
	}
	path_length = wide_string_length(
	               path );

	if( ( path_length != 1 )
	 || ( path[ 0 ] != (wchar_t) '\\' ) )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_ARGUMENTS,
		 LIBCERROR_ARGUMENT_ERROR_UNSUPPORTED_VALUE,
		 "%s: unsupported path: %ls.",
		 function,
		 path );

		result = -ERROR_FILE_NOT_FOUND;

		goto on_error;
	}
	return( 0 );

on_error:
	if( error != NULL )
	{
		libcnotify_print_error_backtrace(
		 error );
		libcerror_error_free(
		 &error );
	}
	return( result );
}

/* Closes a file or direcotry
 * Returns 0 if successful or a negative error code otherwise
 */
int __stdcall qcowmount_dokan_CloseFile(
               const wchar_t *path,
               DOKAN_FILE_INFO *file_info QCOWTOOLS_ATTRIBUTE_UNUSED )
{
	libcerror_error_t *error = NULL;
	static char *function    = "qcowmount_dokan_CloseFile";
	int result               = 0;

	QCOWTOOLS_UNREFERENCED_PARAMETER( file_info )

	if( path == NULL )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_ARGUMENTS,
		 LIBCERROR_ARGUMENT_ERROR_INVALID_VALUE,
		 "%s: invalid path.",
		 function );

		result = -ERROR_BAD_ARGUMENTS;

		goto on_error;
	}
	return( 0 );

on_error:
	if( error != NULL )
	{
		libcnotify_print_error_backtrace(
		 error );
		libcerror_error_free(
		 &error );
	}
	return( result );
}

/* Reads a buffer of data at the specified offset
 * Returns 0 if successful or a negative error code otherwise
 */
int __stdcall qcowmount_dokan_ReadFile(
               const wchar_t *path,
               void *buffer,
               DWORD number_of_bytes_to_read,
               DWORD *number_of_bytes_read,
               LONGLONG offset,
               DOKAN_FILE_INFO *file_info QCOWTOOLS_ATTRIBUTE_UNUSED )
{
	libcerror_error_t *error = NULL;
	static char *function    = "qcowmount_dokan_ReadFile";
	size_t path_length       = 0;
	ssize_t read_count       = 0;
	int input_file_index     = 0;
	int result               = 0;
	int string_index         = 0;

	QCOWTOOLS_UNREFERENCED_PARAMETER( file_info )

	if( path == NULL )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_ARGUMENTS,
		 LIBCERROR_ARGUMENT_ERROR_INVALID_VALUE,
		 "%s: invalid path.",
		 function );

		result = -ERROR_BAD_ARGUMENTS;

		goto on_error;
	}
	if( number_of_bytes_to_read > (DWORD) INT32_MAX )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_ARGUMENTS,
		 LIBCERROR_ARGUMENT_ERROR_VALUE_EXCEEDS_MAXIMUM,
		 "%s: invalid number of bytes to read value exceeds maximum.",
		 function );

		result = -ERROR_BAD_ARGUMENTS;

		goto on_error;
	}
	if( number_of_bytes_read == NULL )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_ARGUMENTS,
		 LIBCERROR_ARGUMENT_ERROR_INVALID_VALUE,
		 "%s: invalid number of bytes read.",
		 function );

		result = -ERROR_BAD_ARGUMENTS;

		goto on_error;
	}
	path_length = wide_string_length(
	               path );

	if( ( path_length <= qcowmount_dokan_path_prefix_length )
         || ( path_length > ( qcowmount_dokan_path_prefix_length + 3 ) )
	 || ( wide_string_compare(
	       path,
	       qcowmount_dokan_path_prefix,
	       qcowmount_dokan_path_prefix_length ) != 0 ) )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_ARGUMENTS,
		 LIBCERROR_ARGUMENT_ERROR_UNSUPPORTED_VALUE,
		 "%s: unsupported path: %ls.",
		 function,
		 path );

		result = -ERROR_FILE_NOT_FOUND;

		goto on_error;
	}
	string_index = (int) qcowmount_dokan_path_prefix_length;

	input_file_index = path[ string_index++ ] - (wchar_t) '0';

	if( string_index < (int) path_length )
	{
		input_file_index *= 10;
		input_file_index += path[ string_index++ ] - (wchar_t) '0';
	}
	if( string_index < (int) path_length )
	{
		input_file_index *= 10;
		input_file_index += path[ string_index++ ] - (wchar_t) '0';
	}
	input_file_index -= 1;

	if( mount_handle_seek_offset(
	     qcowmount_mount_handle,
	     input_file_index,
	     (off64_t) offset,
	     SEEK_SET,
	     &error ) == -1 )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_IO,
		 LIBCERROR_IO_ERROR_SEEK_FAILED,
		 "%s: unable to seek offset in mount handle.",
		 function );

		result = -ERROR_SEEK_ON_DEVICE;

		goto on_error;
	}
	read_count = mount_handle_read_buffer(
		      qcowmount_mount_handle,
		      input_file_index,
		      (uint8_t *) buffer,
		      (size_t) number_of_bytes_to_read,
		      &error );

	if( read_count == -1 )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_IO,
		 LIBCERROR_IO_ERROR_READ_FAILED,
		 "%s: unable to read from mount handle.",
		 function );

		result = -ERROR_READ_FAULT;

		goto on_error;
	}
	if( read_count > (size_t) INT32_MAX )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_ARGUMENTS,
		 LIBCERROR_ARGUMENT_ERROR_VALUE_EXCEEDS_MAXIMUM,
		 "%s: invalid read count value exceeds maximum.",
		 function );

		result = -ERROR_READ_FAULT;

		goto on_error;
	}
	/* Dokan does not require the read function to return ERROR_HANDLE_EOF
	 */
	*number_of_bytes_read = (DWORD) read_count;

	return( 0 );

on_error:
	if( error != NULL )
	{
		libcnotify_print_error_backtrace(
		 error );
		libcerror_error_free(
		 &error );
	}
	return( result );
}

/* Sets the values in a find data structure
 * Returns 1 if successful or -1 on error
 */
int qcowmount_dokan_set_find_data(
     WIN32_FIND_DATAW *find_data,
     size64_t size,
     int number_of_sub_items,
     uint8_t use_mount_time,
     libcerror_error_t **error )
{
	static char *function = "qcowmount_dokan_set_find_data";

	if( find_data == NULL )
	{
		libcerror_error_set(
		 error,
		 LIBCERROR_ERROR_DOMAIN_ARGUMENTS,
		 LIBCERROR_ARGUMENT_ERROR_INVALID_VALUE,
		 "%s: invalid find data.",
		 function );

		return( -1 );
	}
/* TODO implement
	if( use_mount_time != 0 )
	{
	}
*/
	if( size > 0 )
	{
		find_data->nFileSizeHigh = (DWORD) ( size >> 32 );
		find_data->nFileSizeLow  = (DWORD) ( size & 0xffffffffUL );
	}
	find_data->dwFileAttributes = FILE_ATTRIBUTE_READONLY;

	if( number_of_sub_items > 0 )
	{
		find_data->dwFileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
	}
	return( 1 );
}

/* Fills a directory entry
 * Returns 1 if successful or -1 on error
 */
int qcowmount_dokan_filldir(
     PFillFindData fill_find_data,
     DOKAN_FILE_INFO *file_info,
     wchar_t *name,
     size_t name_size,
     WIN32_FIND_DATAW *find_data,
     mount_handle_t *mount_handle,
     int input_file_index,
     uint8_t use_mount_time,
     libcerror_error_t **error )
{
	static char *function   = "qcowmount_dokan_filldir";
	size64_t media_size     = 0;
	int number_of_sub_items = 0;

	if( fill_find_data == NULL )
	{
		libcerror_error_set(
		 error,
		 LIBCERROR_ERROR_DOMAIN_ARGUMENTS,
		 LIBCERROR_ARGUMENT_ERROR_INVALID_VALUE,
		 "%s: invalid fill find data.",
		 function );

		return( -1 );
	}
	if( name == NULL )
	{
		libcerror_error_set(
		 error,
		 LIBCERROR_ERROR_DOMAIN_ARGUMENTS,
		 LIBCERROR_ARGUMENT_ERROR_INVALID_VALUE,
		 "%s: invalid name.",
		 function );

		return( -1 );
	}
	if( name_size > (size_t) MAX_PATH )
	{
		libcerror_error_set(
		 error,
		 LIBCERROR_ERROR_DOMAIN_ARGUMENTS,
		 LIBCERROR_ARGUMENT_ERROR_VALUE_OUT_OF_BOUNDS,
		 "%s: invalid name size value out of bounds.",
		 function );

		return( -1 );
	}
	if( mount_handle == NULL )
	{
		number_of_sub_items = 1;
	}
	else
	{
		if( mount_handle_get_media_size(
		     mount_handle,
		     input_file_index,
		     &media_size,
		     error ) != 1 )
		{
			libcerror_error_set(
			 error,
			 LIBCERROR_ERROR_DOMAIN_RUNTIME,
			 LIBCERROR_RUNTIME_ERROR_GET_FAILED,
			 "%s: unable to retrieve media size.",
			 function );

			return( -1 );
		}
	}
	if( memory_set(
	     find_data,
	     0,
	     sizeof( WIN32_FIND_DATAW ) ) == NULL )
	{
		libcerror_error_set(
		 error,
		 LIBCERROR_ERROR_DOMAIN_MEMORY,
		 LIBCERROR_MEMORY_ERROR_SET_FAILED,
		 "%s: unable to clear find data.",
		 function );

		return( -1 );
	}
	if( wide_string_copy(
	     find_data->cFileName,
	     name,
	     name_size ) == NULL )
	{
		libcerror_error_set(
		 error,
		 LIBCERROR_ERROR_DOMAIN_MEMORY,
		 LIBCERROR_MEMORY_ERROR_COPY_FAILED,
		 "%s: unable to copy filename.",
		 function );

		return( -1 );
	}
	if( name_size <= (size_t) 14 )
	{
		if( wide_string_copy(
		     find_data->cAlternateFileName,
		     name,
		     name_size ) == NULL )
		{
			libcerror_error_set(
			 error,
			 LIBCERROR_ERROR_DOMAIN_MEMORY,
			 LIBCERROR_MEMORY_ERROR_COPY_FAILED,
			 "%s: unable to copy alternate filename.",
			 function );

			return( -1 );
		}
	}
	if( qcowmount_dokan_set_find_data(
	     find_data,
	     media_size,
	     number_of_sub_items,
	     use_mount_time,
	     error ) != 1 )
	{
		libcerror_error_set(
		 error,
		 LIBCERROR_ERROR_DOMAIN_RUNTIME,
		 LIBCERROR_RUNTIME_ERROR_SET_FAILED,
		 "%s: unable to set find data.",
		 function );

		return( -1 );
	}
	if( fill_find_data(
	     find_data,
	     file_info ) != 0 )
	{
		libcerror_error_set(
		 error,
		 LIBCERROR_ERROR_DOMAIN_RUNTIME,
		 LIBCERROR_RUNTIME_ERROR_SET_FAILED,
		 "%s: unable to set directory entry.",
		 function );

		return( -1 );
	}
	return( 1 );
}

/* Reads a directory
 * Returns 0 if successful or a negative error code otherwise
 */
int __stdcall qcowmount_dokan_FindFiles(
               const wchar_t *path,
               PFillFindData fill_find_data,
               DOKAN_FILE_INFO *file_info )
{
	WIN32_FIND_DATAW find_data;

	wchar_t qcowmount_dokan_path[ 10 ];

	libcerror_error_t *error  = NULL;
	static char *function     = "qcowmount_dokan_FindFiles";
	size_t path_length        = 0;
	int input_file_index      = 0;
	int number_of_input_files = 0;
	int result                = 0;
	int string_index          = 0;

	if( path == NULL )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_ARGUMENTS,
		 LIBCERROR_ARGUMENT_ERROR_INVALID_VALUE,
		 "%s: invalid path.",
		 function );

		result = -ERROR_BAD_ARGUMENTS;

		goto on_error;
	}
	path_length = wide_string_length(
	               path );

	if( ( path_length != 1 )
	 || ( path[ 0 ] != (wchar_t) '\\' ) )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_ARGUMENTS,
		 LIBCERROR_ARGUMENT_ERROR_UNSUPPORTED_VALUE,
		 "%s: unsupported path: %ls.",
		 function,
		 path );

		result = -ERROR_FILE_NOT_FOUND;

		goto on_error;
	}
	if( wide_string_copy(
	     qcowmount_dokan_path,
	     qcowmount_dokan_path_prefix,
	     qcowmount_dokan_path_prefix_length ) == NULL )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_MEMORY,
		 LIBCERROR_MEMORY_ERROR_COPY_FAILED,
		 "%s: unable to copy path prefix.",
		 function );

		result = -ERROR_GEN_FAILURE;

		goto on_error;
	}
	if( mount_handle_get_number_of_input_files(
	     qcowmount_mount_handle,
	     &number_of_input_files,
	     &error ) != 1 )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_RUNTIME,
		 LIBCERROR_RUNTIME_ERROR_GET_FAILED,
		 "%s: unable to retrieve number of input files.",
		 function );

		result = -ERROR_GEN_FAILURE;

		goto on_error;
	}
	if( ( number_of_input_files < 0 )
	 || ( number_of_input_files > 99 ) )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_ARGUMENTS,
		 LIBCERROR_ARGUMENT_ERROR_UNSUPPORTED_VALUE,
		 "%s: unsupported number of input files.",
		 function );

		result = -ERROR_GEN_FAILURE;

		goto on_error;
	}
	if( qcowmount_dokan_filldir(
	     fill_find_data,
	     file_info,
	     L".",
	     2,
	     &find_data,
	     NULL,
	     -1,
	     1,
	     &error ) != 1 )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_RUNTIME,
		 LIBCERROR_RUNTIME_ERROR_SET_FAILED,
		 "%s: unable to set find data.",
		 function );

		result = -ERROR_GEN_FAILURE;

		goto on_error;
	}
	if( qcowmount_dokan_filldir(
	     fill_find_data,
	     file_info,
	     L"..",
	     3,
	     &find_data,
	     NULL,
	     -1,
	     0,
	     &error ) != 1 )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_RUNTIME,
		 LIBCERROR_RUNTIME_ERROR_SET_FAILED,
		 "%s: unable to set find data.",
		 function );

		result = -ERROR_GEN_FAILURE;

		goto on_error;
	}
	for( input_file_index = 1;
	     input_file_index <= number_of_input_files;
	     input_file_index++ )
	{
		string_index = (int) qcowmount_dokan_path_prefix_length;

		if( input_file_index >= 100 )
		{
			qcowmount_dokan_path[ string_index++ ] = (wchar_t) ( '0' + ( input_file_index / 100 ) );
		}
		if( input_file_index >= 10 )
		{
			qcowmount_dokan_path[ string_index++ ] = (wchar_t) ( '0' + ( input_file_index / 10 ) );
		}
		qcowmount_dokan_path[ string_index++ ] = (wchar_t) ( '0' + ( input_file_index % 10 ) );
		qcowmount_dokan_path[ string_index++ ] = 0;

/* TODO add support for multiple input files ? */
		if( input_file_index != 1 )
		{
			libcerror_error_set(
			 &error,
			 LIBCERROR_ERROR_DOMAIN_RUNTIME,
			 LIBCERROR_RUNTIME_ERROR_VALUE_OUT_OF_BOUNDS,
			 "%s: invalid input file index value out of bounds.",
			 function );

			result = -ERROR_BAD_ARGUMENTS;

			goto on_error;
		}
		if( qcowmount_dokan_filldir(
		     fill_find_data,
		     file_info,
		     &( qcowmount_dokan_path[ 1 ] ),
		     string_index - 1,
		     &find_data,
		     qcowmount_mount_handle,
		     input_file_index - 1,
		     1,
		     &error ) != 1 )
		{
			libcerror_error_set(
			 &error,
			 LIBCERROR_ERROR_DOMAIN_RUNTIME,
			 LIBCERROR_RUNTIME_ERROR_SET_FAILED,
			 "%s: unable to set find data.",
			 function );

			result = -ERROR_GEN_FAILURE;

			goto on_error;
		}
	}
	return( 0 );

on_error:
	if( error != NULL )
	{
		libcnotify_print_error_backtrace(
		 error );
		libcerror_error_free(
		 &error );
	}
	return( result );
}

/* Sets the values in a file information structure
 * Returns 1 if successful or -1 on error
 */
int qcowmount_dokan_set_file_information(
     BY_HANDLE_FILE_INFORMATION *file_information,
     size64_t size,
     int number_of_sub_items,
     uint8_t use_mount_time,
     libcerror_error_t **error )
{
	static char *function = "qcowmount_dokan_set_file_information";

	if( file_information == NULL )
	{
		libcerror_error_set(
		 error,
		 LIBCERROR_ERROR_DOMAIN_ARGUMENTS,
		 LIBCERROR_ARGUMENT_ERROR_INVALID_VALUE,
		 "%s: invalid file information.",
		 function );

		return( -1 );
	}
/* TODO implement
	if( use_mount_time != 0 )
	{
	}
*/
	if( size > 0 )
	{
		file_information->nFileSizeHigh = (DWORD) ( size >> 32 );
		file_information->nFileSizeLow  = (DWORD) ( size & 0xffffffffUL );
	}
	file_information->dwFileAttributes = FILE_ATTRIBUTE_READONLY;

	if( number_of_sub_items > 0 )
	{
		file_information->dwFileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
	}
	return( 1 );
}

/* Retrieves the file information
 * Returns 0 if successful or a negative error code otherwise
 */
int __stdcall qcowmount_dokan_GetFileInformation(
               const wchar_t *path,
               BY_HANDLE_FILE_INFORMATION *file_information,
               DOKAN_FILE_INFO *file_info )
{
	libcerror_error_t *error = NULL;
	static char *function    = "qcowmount_dokan_GetFileInformation";
	size64_t media_size      = 0;
	size_t path_length       = 0;
	int input_file_index     = 0;
	int number_of_sub_items  = 0;
	int result               = 0;
	int string_index         = 0;
	uint8_t use_mount_time   = 0;

	if( path == NULL )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_ARGUMENTS,
		 LIBCERROR_ARGUMENT_ERROR_INVALID_VALUE,
		 "%s: invalid path.",
		 function );

		result = -ERROR_BAD_ARGUMENTS;

		goto on_error;
	}
	if( file_info == NULL )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_ARGUMENTS,
		 LIBCERROR_ARGUMENT_ERROR_INVALID_VALUE,
		 "%s: invalid file info.",
		 function );

		result = -ERROR_BAD_ARGUMENTS;

		goto on_error;
	}
	path_length = wide_string_length(
	               path );

	if( path_length == 1 )
	{
		if( path[ 0 ] != (wchar_t) '\\' )
		{
			libcerror_error_set(
			 &error,
			 LIBCERROR_ERROR_DOMAIN_ARGUMENTS,
			 LIBCERROR_ARGUMENT_ERROR_UNSUPPORTED_VALUE,
			 "%s: unsupported path: %ls.",
			 function,
			 path );

			result = -ERROR_FILE_NOT_FOUND;

			goto on_error;
		}
		number_of_sub_items = 1;
		use_mount_time      = 1;
	}
	else
	{
		if( ( path_length <= qcowmount_dokan_path_prefix_length )
		 || ( path_length > ( qcowmount_dokan_path_prefix_length + 3 ) )
		 || ( wide_string_compare(
		       path,
		       qcowmount_dokan_path_prefix,
		       qcowmount_dokan_path_prefix_length ) != 0 ) )
		{
			libcerror_error_set(
			 &error,
			 LIBCERROR_ERROR_DOMAIN_ARGUMENTS,
			 LIBCERROR_ARGUMENT_ERROR_UNSUPPORTED_VALUE,
			 "%s: unsupported path: %ls.",
			 function,
			 path );

			result = -ERROR_FILE_NOT_FOUND;

			goto on_error;
		}
		string_index = (int) qcowmount_dokan_path_prefix_length;

		input_file_index = path[ string_index++ ] - (wchar_t) '0';

		if( string_index < (int) path_length )
		{
			input_file_index *= 10;
			input_file_index += path[ string_index++ ] - (wchar_t) '0';
		}
		if( string_index < (int) path_length )
		{
			input_file_index *= 10;
			input_file_index += path[ string_index++ ] - (wchar_t) '0';
		}
		input_file_index -= 1;

/* TODO add support for multiple input files ? */
		if( input_file_index != 0 )
		{
			libcerror_error_set(
			 &error,
			 LIBCERROR_ERROR_DOMAIN_RUNTIME,
			 LIBCERROR_RUNTIME_ERROR_VALUE_OUT_OF_BOUNDS,
			 "%s: invalid input file index value out of bounds.",
			 function );

			result = -ERROR_BAD_ARGUMENTS;

			goto on_error;
		}
		if( mount_handle_get_media_size(
		     qcowmount_mount_handle,
		     input_file_index,
		     &media_size,
		     &error ) != 1 )
		{
			libcerror_error_set(
			 &error,
			 LIBCERROR_ERROR_DOMAIN_RUNTIME,
			 LIBCERROR_RUNTIME_ERROR_GET_FAILED,
			 "%s: unable to retrieve media size.",
			 function );

			result = -ERROR_GEN_FAILURE;

			goto on_error;
		}
		use_mount_time = 1;
	}
	if( qcowmount_dokan_set_file_information(
	     file_information,
	     media_size,
	     number_of_sub_items,
	     use_mount_time,
	     &error ) != 1 )
	{
		libcerror_error_set(
		 &error,
		 LIBCERROR_ERROR_DOMAIN_RUNTIME,
		 LIBCERROR_RUNTIME_ERROR_SET_FAILED,
		 "%s: unable to set file info.",
		 function );

		result = -ERROR_GEN_FAILURE;

		goto on_error;
	}
	return( 0 );

on_error:
	if( error != NULL )
	{
		libcnotify_print_error_backtrace(
		 error );
		libcerror_error_free(
		 &error );
	}
	return( result );
}

/* Retrieves the volume information
 * Returns 0 if successful or a negative error code otherwise
 */
int __stdcall qcowmount_dokan_GetVolumeInformation(
               wchar_t *volume_name,
               DWORD volume_name_size,
               DWORD *volume_serial_number,
               DWORD *maximum_filename_length,
               DWORD *file_system_flags,
               wchar_t *file_system_name,
               DWORD file_system_name_size,
               DOKAN_FILE_INFO *file_info QCOWTOOLS_ATTRIBUTE_UNUSED )
{
	libcerror_error_t *error = NULL;
	static char *function    = "qcowmount_dokan_GetVolumeInformation";
	int result               = 0;

	QCOWTOOLS_UNREFERENCED_PARAMETER( file_info )

	if( ( volume_name != NULL )
	 && ( volume_name_size > (DWORD) ( sizeof( wchar_t ) * 5 ) ) )
	{
		/* Using wcsncpy seems to cause strange behavior here
		 */
		if( memory_copy(
		     volume_name,
		     L"QCOW",
		     sizeof( wchar_t ) * 5 ) == NULL )
		{
			libcerror_error_set(
			 &error,
			 LIBCERROR_ERROR_DOMAIN_MEMORY,
			 LIBCERROR_MEMORY_ERROR_COPY_FAILED,
			 "%s: unable to copy volume name.",
			 function );

			result = -ERROR_GEN_FAILURE;

			goto on_error;
		}
	}
	if( volume_serial_number != NULL )
	{
		/* If this value contains 0 it can crash the system is this an issue in Dokan?
		 */
		*volume_serial_number = 0x19831116;
	}
	if( maximum_filename_length != NULL )
	{
		*maximum_filename_length = 256;
	}
	if( file_system_flags != NULL )
	{
		*file_system_flags = FILE_CASE_SENSITIVE_SEARCH
		                   | FILE_CASE_PRESERVED_NAMES
		                   | FILE_UNICODE_ON_DISK
		                   | FILE_READ_ONLY_VOLUME;
	}
	if( ( file_system_name != NULL )
	 && ( file_system_name_size > (DWORD) ( sizeof( wchar_t ) * 6 ) ) )
	{
		/* Using wcsncpy seems to cause strange behavior here
		 */
		if( memory_copy(
		     file_system_name,
		     L"Dokan",
		     sizeof( wchar_t ) * 6 ) == NULL )
		{
			libcerror_error_set(
			 &error,
			 LIBCERROR_ERROR_DOMAIN_MEMORY,
			 LIBCERROR_MEMORY_ERROR_COPY_FAILED,
			 "%s: unable to copy file system name.",
			 function );

			result = -ERROR_GEN_FAILURE;

			goto on_error;
		}
	}
	return( 0 );

on_error:
	if( error != NULL )
	{
		libcnotify_print_error_backtrace(
		 error );
		libcerror_error_free(
		 &error );
	}
	return( result );
}

/* Unmount the image
 * Returns 0 if successful or a negative error code otherwise
 */
int __stdcall qcowmount_dokan_Unmount(
               DOKAN_FILE_INFO *file_info QCOWTOOLS_ATTRIBUTE_UNUSED )
{
	static char *function = "qcowmount_dokan_Unmount";

	QCOWTOOLS_UNREFERENCED_PARAMETER( file_info )

	return( 0 );
}

#endif

/* The main program
 */
#if defined( HAVE_WIDE_SYSTEM_CHARACTER )
int wmain( int argc, wchar_t * const argv[] )
#else
int main( int argc, char * const argv[] )
#endif
{
	libqcow_error_t *error                      = NULL;
	system_character_t *mount_point             = NULL;
	system_character_t *option_extended_options = NULL;
	system_character_t *option_keys             = NULL;
	system_character_t *option_password         = NULL;
	system_character_t *source                  = NULL;
	char *program                               = "qcowmount";
	system_integer_t option                     = 0;
	int result                                  = 0;
	int verbose                                 = 0;

#if defined( HAVE_LIBFUSE ) || defined( HAVE_LIBOSXFUSE )
	struct fuse_operations qcowmount_fuse_operations;

	struct fuse_args qcowmount_fuse_arguments   = FUSE_ARGS_INIT(0, NULL);
	struct fuse_chan *qcowmount_fuse_channel    = NULL;
	struct fuse *qcowmount_fuse_handle          = NULL;

#elif defined( HAVE_LIBDOKAN )
	DOKAN_OPERATIONS qcowmount_dokan_operations;
	DOKAN_OPTIONS qcowmount_dokan_options;
#endif

	libcnotify_stream_set(
	 stderr,
	 NULL );
	libcnotify_verbose_set(
	 1 );

	if( libclocale_initialize(
             "qcowtools",
	     &error ) != 1 )
	{
		fprintf(
		 stderr,
		 "Unable to initialize locale values.\n" );

		goto on_error;
	}
	if( qcowtools_output_initialize(
             _IONBF,
             &error ) != 1 )
	{
		fprintf(
		 stderr,
		 "Unable to initialize output settings.\n" );

		goto on_error;
	}
	qcowoutput_version_fprint(
	 stdout,
	 program );

	while( ( option = qcowtools_getopt(
	                   argc,
	                   argv,
	                   _SYSTEM_STRING( "hk:p:vVX:" ) ) ) != (system_integer_t) -1 )
	{
		switch( option )
		{
			case (system_integer_t) '?':
			default:
				fprintf(
				 stderr,
				 "Invalid argument: %" PRIs_SYSTEM "\n",
				 argv[ optind - 1 ] );

				usage_fprint(
				 stdout );

				return( EXIT_FAILURE );

			case (system_integer_t) 'h':
				usage_fprint(
				 stdout );

				return( EXIT_SUCCESS );

			case (system_integer_t) 'k':
				option_keys = optarg;

				break;

			case (system_integer_t) 'p':
				option_password = optarg;

				break;

			case (system_integer_t) 'v':
				verbose = 1;

				break;

			case (system_integer_t) 'V':
				qcowoutput_copyright_fprint(
				 stdout );

				return( EXIT_SUCCESS );

			case (system_integer_t) 'X':
				option_extended_options = optarg;

				break;
		}
	}
	if( optind == argc )
	{
		fprintf(
		 stderr,
		 "Missing source file.\n" );

		usage_fprint(
		 stdout );

		return( EXIT_FAILURE );
	}
	source = argv[ optind++ ];

	if( optind == argc )
	{
		fprintf(
		 stderr,
		 "Missing mount point.\n" );

		usage_fprint(
		 stdout );

		return( EXIT_FAILURE );
	}
	mount_point = argv[ optind ];

	libcnotify_verbose_set(
	 verbose );
	libqcow_notify_set_stream(
	 stderr,
	 NULL );
	libqcow_notify_set_verbose(
	 verbose );

	if( mount_handle_initialize(
	     &qcowmount_mount_handle,
	     &error ) != 1 )
	{
		fprintf(
		 stderr,
		 "Unable to initialize mount handle.\n" );

		goto on_error;
	}
	if( option_keys != NULL )
	{
		if( mount_handle_set_keys(
		     qcowmount_mount_handle,
		     option_keys,
		     &error ) != 1 )
		{
			fprintf(
			 stderr,
			 "Unable to set keys.\n" );

			goto on_error;
		}
	}
	if( option_password != NULL )
	{
		if( mount_handle_set_password(
		     qcowmount_mount_handle,
		     option_password,
		     &error ) != 1 )
		{
			fprintf(
			 stderr,
			 "Unable to set password.\n" );

			goto on_error;
		}
	}
	if( mount_handle_open_input(
	     qcowmount_mount_handle,
	     source,
	     &error ) != 1 )
	{
		fprintf(
		 stderr,
		 "Unable to open source file.\n" );

		goto on_error;
	}
#if defined( HAVE_LIBFUSE ) || defined( HAVE_LIBOSXFUSE )
	if( memory_set(
	     &qcowmount_fuse_operations,
	     0,
	     sizeof( struct fuse_operations ) ) == NULL )
	{
		fprintf(
		 stderr,
		 "Unable to clear fuse operations.\n" );

		goto on_error;
	}
	if( option_extended_options != NULL )
	{
		/* This argument is required but ignored
		 */
		if( fuse_opt_add_arg(
		     &qcowmount_fuse_arguments,
		     "" ) != 0 )
		{
			fprintf(
			 stderr,
			 "Unable add fuse arguments.\n" );

			goto on_error;
		}
		if( fuse_opt_add_arg(
		     &qcowmount_fuse_arguments,
		     "-o" ) != 0 )
		{
			fprintf(
			 stderr,
			 "Unable add fuse arguments.\n" );

			goto on_error;
		}
		if( fuse_opt_add_arg(
		     &qcowmount_fuse_arguments,
		     option_extended_options ) != 0 )
		{
			fprintf(
			 stderr,
			 "Unable add fuse arguments.\n" );

			goto on_error;
		}
	}
	qcowmount_fuse_operations.open    = &qcowmount_fuse_open;
	qcowmount_fuse_operations.read    = &qcowmount_fuse_read;
	qcowmount_fuse_operations.readdir = &qcowmount_fuse_readdir;
	qcowmount_fuse_operations.getattr = &qcowmount_fuse_getattr;
	qcowmount_fuse_operations.destroy = &qcowmount_fuse_destroy;

	qcowmount_fuse_channel = fuse_mount(
	                          mount_point,
	                          &qcowmount_fuse_arguments );

	if( qcowmount_fuse_channel == NULL )
	{
		fprintf(
		 stderr,
		 "Unable to create fuse channel.\n" );

		goto on_error;
	}
	qcowmount_fuse_handle = fuse_new(
	                         qcowmount_fuse_channel,
	                         &qcowmount_fuse_arguments,
	                         &qcowmount_fuse_operations,
	                         sizeof( struct fuse_operations ),
	                         qcowmount_mount_handle );
	
	if( qcowmount_fuse_handle == NULL )
	{
		fprintf(
		 stderr,
		 "Unable to create fuse handle.\n" );

		goto on_error;
	}
	if( verbose == 0 )
	{
		if( fuse_daemonize(
		     0 ) != 0 )
		{
			fprintf(
			 stderr,
			 "Unable to daemonize fuse.\n" );

			goto on_error;
		}
	}
	result = fuse_loop(
	          qcowmount_fuse_handle );

	if( result != 0 )
	{
		fprintf(
		 stderr,
		 "Unable to run fuse loop.\n" );

		goto on_error;
	}
	fuse_destroy(
	 qcowmount_fuse_handle );

	fuse_opt_free_args(
	 &qcowmount_fuse_arguments );

	return( EXIT_SUCCESS );

#elif defined( HAVE_LIBDOKAN )
	if( memory_set(
	     &qcowmount_dokan_operations,
	     0,
	     sizeof( DOKAN_OPERATIONS ) ) == NULL )
	{
		fprintf(
		 stderr,
		 "Unable to clear dokan operations.\n" );

		goto on_error;
	}
	if( memory_set(
	     &qcowmount_dokan_options,
	     0,
	     sizeof( DOKAN_OPTIONS ) ) == NULL )
	{
		fprintf(
		 stderr,
		 "Unable to clear dokan options.\n" );

		goto on_error;
	}
	qcowmount_dokan_options.Version     = 600;
	qcowmount_dokan_options.ThreadCount = 0;
	qcowmount_dokan_options.MountPoint  = mount_point;

	if( verbose != 0 )
	{
		qcowmount_dokan_options.Options |= DOKAN_OPTION_STDERR;
#if defined( HAVE_DEBUG_OUTPUT )
		qcowmount_dokan_options.Options |= DOKAN_OPTION_DEBUG;
#endif
	}
/* This will only affect the drive properties
	qcowmount_dokan_options.Options |= DOKAN_OPTION_REMOVABLE;
*/
	qcowmount_dokan_options.Options |= DOKAN_OPTION_KEEP_ALIVE;

	qcowmount_dokan_operations.CreateFile           = &qcowmount_dokan_CreateFile;
	qcowmount_dokan_operations.OpenDirectory        = &qcowmount_dokan_OpenDirectory;
	qcowmount_dokan_operations.CreateDirectory      = NULL;
	qcowmount_dokan_operations.Cleanup              = NULL;
	qcowmount_dokan_operations.CloseFile            = &qcowmount_dokan_CloseFile;
	qcowmount_dokan_operations.ReadFile             = &qcowmount_dokan_ReadFile;
	qcowmount_dokan_operations.WriteFile            = NULL;
	qcowmount_dokan_operations.FlushFileBuffers     = NULL;
	qcowmount_dokan_operations.GetFileInformation   = &qcowmount_dokan_GetFileInformation;
	qcowmount_dokan_operations.FindFiles            = &qcowmount_dokan_FindFiles;
	qcowmount_dokan_operations.FindFilesWithPattern = NULL;
	qcowmount_dokan_operations.SetFileAttributes    = NULL;
	qcowmount_dokan_operations.SetFileTime          = NULL;
	qcowmount_dokan_operations.DeleteFile           = NULL;
	qcowmount_dokan_operations.DeleteDirectory      = NULL;
	qcowmount_dokan_operations.MoveFile             = NULL;
	qcowmount_dokan_operations.SetEndOfFile         = NULL;
	qcowmount_dokan_operations.SetAllocationSize    = NULL;
	qcowmount_dokan_operations.LockFile             = NULL;
	qcowmount_dokan_operations.UnlockFile           = NULL;
	qcowmount_dokan_operations.GetFileSecurity      = NULL;
	qcowmount_dokan_operations.SetFileSecurity      = NULL;
	qcowmount_dokan_operations.GetDiskFreeSpace     = NULL;
	qcowmount_dokan_operations.GetVolumeInformation = &qcowmount_dokan_GetVolumeInformation;
	qcowmount_dokan_operations.Unmount              = &qcowmount_dokan_Unmount;

	result = DokanMain(
	          &qcowmount_dokan_options,
	          &qcowmount_dokan_operations );

	switch( result )
	{
		case DOKAN_SUCCESS:
			break;

		case DOKAN_ERROR:
			fprintf(
			 stderr,
			 "Unable to run dokan main: generic error\n" );
			break;

		case DOKAN_DRIVE_LETTER_ERROR:
			fprintf(
			 stderr,
			 "Unable to run dokan main: bad drive letter\n" );
			break;

		case DOKAN_DRIVER_INSTALL_ERROR:
			fprintf(
			 stderr,
			 "Unable to run dokan main: unable to load driver\n" );
			break;

		case DOKAN_START_ERROR:
			fprintf(
			 stderr,
			 "Unable to run dokan main: driver error\n" );
			break;

		case DOKAN_MOUNT_ERROR:
			fprintf(
			 stderr,
			 "Unable to run dokan main: unable to assign drive letter\n" );
			break;

		case DOKAN_MOUNT_POINT_ERROR:
			fprintf(
			 stderr,
			 "Unable to run dokan main: mount point error\n" );
			break;

		default:
			fprintf(
			 stderr,
			 "Unable to run dokan main: unknown error: %d\n",
			 result );
			break;
	}
	return( EXIT_SUCCESS );
#else
	fprintf(
	 stderr,
	 "No sub system to mount QCOW format.\n" );

	return( EXIT_FAILURE );
#endif

on_error:
	if( error != NULL )
	{
		libcnotify_print_error_backtrace(
		 error );
		libcerror_error_free(
		 &error );
	}
#if defined( HAVE_LIBFUSE ) || defined( HAVE_LIBOSXFUSE )
	if( qcowmount_fuse_handle != NULL )
	{
		fuse_destroy(
		 qcowmount_fuse_handle );
	}
	fuse_opt_free_args(
	 &qcowmount_fuse_arguments );
#endif
	if( qcowmount_mount_handle != NULL )
	{
		mount_handle_free(
		 &qcowmount_mount_handle,
		 NULL );
	}
	return( EXIT_FAILURE );
}

