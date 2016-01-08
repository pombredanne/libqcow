#!/bin/bash
#
# Python-bindings open close testing script
#
# Copyright (C) 2010-2016, Joachim Metz <joachim.metz@gmail.com>
#
# Refer to AUTHORS for acknowledgements.
#
# This software is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This software is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this software.  If not, see <http://www.gnu.org/licenses/>.
#

EXIT_SUCCESS=0;
EXIT_FAILURE=1;
EXIT_IGNORE=77;

list_contains()
{
	LIST=$1;
	SEARCH=$2;

	for LINE in $LIST;
	do
		if test $LINE = $SEARCH;
		then
			return ${EXIT_SUCCESS};
		fi
	done

	return ${EXIT_FAILURE};
}

test_open_close()
{ 
	INPUT_FILE=$1;

	rm -rf tmp;
	mkdir tmp;

	echo "Testing open close of input: ${INPUT_FILE}";

	if test `uname -s` = 'Darwin';
	then
		DYLD_LIBRARY_PATH="../libqcow/.libs/" PYTHONPATH="../pyqcow/.libs/" ${PYTHON} ${SCRIPT} ${INPUT_FILE};
		RESULT=$?;
	else
		LD_LIBRARY_PATH="../libqcow/.libs/" PYTHONPATH="../pyqcow/.libs/" ${PYTHON} ${SCRIPT} ${INPUT_FILE};
		RESULT=$?;
	fi

	rm -rf tmp;

	return ${RESULT};
}

test_open_close_password()
{ 
	DIRNAME=$1;
	INPUT_FILE=$2;
	BASENAME=`basename ${INPUT_FILE}`;
	RESULT=${EXIT_FAILURE};
	PASSWORDFILE="input/.pyqcow/${DIRNAME}/${BASENAME}.password";

	if test -f "${PASSWORDFILE}";
	then
		rm -rf tmp;
		mkdir tmp;

		PASSWORD=`cat "${PASSWORDFILE}" | head -n 1 | sed 's/[\r\n]*$//'`;

		echo "Testing open close with password of input: ${INPUT_FILE}";

		if test `uname -s` = 'Darwin';
		then
			DYLD_LIBRARY_PATH="../libqcow/.libs/" PYTHONPATH="../pyqcow/.libs/" ${PYTHON} ${SCRIPT} -p${PASSWORD} ${INPUT_FILE};
			RESULT=$?;
		else
			LD_LIBRARY_PATH="../libqcow/.libs/" PYTHONPATH="../pyqcow/.libs/" ${PYTHON} ${SCRIPT} -p${PASSWORD} ${INPUT_FILE};
			RESULT=$?;
		fi

		rm -rf tmp;

		echo "";
	else
		echo "Testing open close with password of input: ${INPUT_FILE} (FAIL)";
	fi

	return ${RESULT};
}

PYTHON=`which python${PYTHON_VERSION} 2> /dev/null`;

if ! test -x ${PYTHON};
then
	echo "Missing executable: ${PYTHON}";

	exit ${EXIT_FAILURE};
fi

if ! test -d "input";
then
	echo "No input directory found.";

	exit ${EXIT_IGNORE};
fi

SCRIPT="pyqcow_test_open_close.py";

if ! test -f ${SCRIPT};
then
	echo "Missing script: ${SCRIPT}";

	exit ${EXIT_FAILURE};
fi

OLDIFS=${IFS};
IFS="
";

RESULT=`ls input/* | tr ' ' '\n' | wc -l`;

if test ${RESULT} -eq 0;
then
	echo "No files or directories found in the input directory.";

	EXIT_RESULT=${EXIT_IGNORE};
else
	IGNORELIST="";

	if test -f "input/.pyqcow/ignore";
	then
		IGNORELIST=`cat input/.pyqcow/ignore | sed '/^#/d'`;
	fi
	for TESTDIR in input/*;
	do
		if test -d "${TESTDIR}";
		then
			DIRNAME=`basename ${TESTDIR}`;

			if ! list_contains "${IGNORELIST}" "${DIRNAME}";
			then
				if test -f "input/.pyqcow/${DIRNAME}/files";
				then
					TEST_FILES=`cat input/.pyqcow/${DIRNAME}/files | sed "s?^?${TESTDIR}/?"`;
				else
					TEST_FILES=`ls -1 ${TESTDIR}/* 2> /dev/null`;
				fi
				for TEST_FILE in ${TEST_FILES};
				do
					BASENAME=`basename ${TEST_FILE}`;

					if test -f "input/.pyqcow/${DIRNAME}/${BASENAME}.password";
					then
						if ! test_open_close_password "${DIRNAME}" "${TEST_FILE}";
						then
							exit ${EXIT_FAILURE};
						fi
					else
						if ! test_open_close "${TEST_FILE}";
						then
							exit ${EXIT_FAILURE};
						fi
					fi
				done
			fi
		fi
	done

	EXIT_RESULT=${EXIT_SUCCESS};
fi

IFS=${OLDIFS};

exit ${EXIT_RESULT};

