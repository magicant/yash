/* Yash: yet another shell */
/* predict.c: command line input prediction */
/* (C) 2016 magicant */

/* This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.  */


#include "../common.h"
#include "predict.h"
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/stat.h>
#include <wchar.h>
#include "../exec.h"
#include "../redir.h"
#include "../strbuf.h"
#include "../util.h"
#include "../variable.h"


/* File stream for the statistics file. */
static FILE *file = NULL;


static bool open_file(void);
static bool maybe_init(void);


/* Opens the statistics file if not yet open.
 * Returns true iff successful. */
bool open_file(void)
{
    if (file != NULL)
	return true;

    const wchar_t *home = getvar(L VAR_HOME);
    if (home == NULL || home[0] != L'/')
	return false;

    // TODO filename should be configurable
    xwcsbuf_T wfilename; // filename = ~/.yash_history2
    wb_init(&wfilename);
    wb_cat(&wfilename, home);
    if (wfilename.contents[wfilename.length - 1] != L'/')
	wb_wccat(&wfilename, L'/');
    wb_cat(&wfilename, L".yash_history2");

    char *mbsfilename = realloc_wcstombs(wb_towcs(&wfilename));
    if (mbsfilename == NULL)
	return false;

    int fd = open(mbsfilename, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
    free(mbsfilename);
    if (fd < 0)
	return false;

    struct stat st;
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)
	    || (st.st_mode & (S_IRWXG | S_IRWXO))) {
	xclose(fd);
	return false;
    }

    fd = move_to_shellfd(fd);
    if (fd < 0)
	return false;

    file = fdopen(fd, "r+");
    if (file == NULL) {
	remove_shellfd(fd);
	xclose(fd);
	return false;
    }

    return true;
}

/* Initialize the prediction module if not yet initialized.
 * Returns true iff successful. */
bool maybe_init(void)
{
    if (!open_file())
	return false;

    // TODO If file was opened successfully, we need to read the contents.
    fseek(file, 0, SEEK_END);

    return true;
}

/* Records the argument command line in the statistics file.
 * Only the first line of `cmdline' is recorded. */
void le_record_entered_command(const wchar_t *cmdline)
{
    if (!maybe_init())
	return; // give up

    // Drop lines other than the first.
    wchar_t *firstline = NULL;
    const wchar_t *newline = wcschr(cmdline, L'\n');
    if (newline != NULL)
	cmdline = firstline = xwcsndup(cmdline, newline - cmdline);

    fwprintf(file, L"_=%ls\nprev_exit_status=%d\n", cmdline, laststatus);

    const wchar_t *pwd = getvar(L VAR_PWD);
    if (pwd != NULL)
	fwprintf(file, L"var:PWD=%ls\n", pwd);

    fputwc(L'\n', file);
    fflush(file);

    free(firstline);
}


// TODO need to support a case where the statistics file is written to by
// another yash process.
// TODO need to lock the statistics file while accessing it.
// TODO need to add an option to disable this feature.


/* vim: set ts=8 sts=4 sw=4 noet tw=80: */
