/*      -*- OpenSAF  -*-
 *
 * Copyright Ericsson AB 2017 - All Rights Reserved.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. This file and program are licensed
 * under the GNU Lesser General Public License Version 2.1, February 1999.
 * The complete license can be accessed from the following location:
 * http://opensource.org/licenses/lgpl-license.php
 * See the Copying file included with the OpenSAF distribution for full
 * licensing terms.
 *
 * Author(s): Ericsson AB
 *
 */

#include <unistd.h>
#include <iostream>

#include "experimental/immcpp/api/demo/omsearchnext.h"
#include "experimental/immcpp/api/demo/omclassmanagement.h"
#include "experimental/immcpp/api/demo/omaccessorget.h"
#include "experimental/immcpp/api/demo/omclasscreate.h"
#include "experimental/immcpp/api/demo/omconfigurationchanges.h"
#include "experimental/immcpp/api/demo/oiruntimeobject.h"

extern void HoldValueCcbCreate();

void usage(void) {
  std::cout << "\nDESCRIPTION\n";
  std::cout << "\tTest C++ IMM Abstractions\n";
  std::cout << "\nOPTIONS\n";
  std::cout << "\t-h \t This help\n";
  std::cout << "\t-o \t Test IMM OM interfaces\n";
  std::cout << "\t-i \t Test IMM OI interfaces\n";
}

int main(int argc, char *argv[]) {
  while (1) {
    int c = getopt(argc, argv, "?oih");
    if (c == -1) {
      break;
    }

    switch (c) {
      case 'o':
        std::cout << "@@ OBJECT SEARCH NEXT @@" << std::endl;
        std::cout << "-- On Multiple Value Attribute Class --" << std::endl;
        std::cout << "===============" << std::endl;
        TestOmSearchNextOnMultipleValuesClass();
        std::cout << std::endl;

        std::cout << "-- On Single Value Attribute Class --" << std::endl;
        std::cout << "===============" << std::endl;
        TestOmSearchNextOnSingleValuesClass();
        std::cout << std::endl;
        std::cout << std::endl;

        std::cout << "@@ CLASS DESCRIPTION GET @@" << std::endl;
        std::cout << "-- On Multiple Value Attribute Class --" << std::endl;
        std::cout << "===============" << std::endl;
        TestOmClassDescriptionGetOnMultipleValueClass();
        std::cout << std::endl;

        std::cout << "-- On Single Value Attribute Class --" << std::endl;
        std::cout << "===============" << std::endl;
        TestOmClassDescriptionGetOnSingleValueClass();
        std::cout << std::endl;
        std::cout << std::endl;

        std::cout << "@@ OBJECT ACCESS GET @@" << std::endl;
        std::cout << "-- On Multiple Value Attribute Class --" << std::endl;
        std::cout << "===============" << std::endl;
        TestOmAccessorGetOnMultipleValueClass();
        std::cout << std::endl;

        std::cout << "-- On Single Value Attribute Class --" << std::endl;
        std::cout << "===============" << std::endl;
        TestOmAccessorGetOnSingleValueClass();
        std::cout << std::endl;
        std::cout << std::endl;

        std::cout << "@@ CREATE CLASS @@" << std::endl;
        std::cout << "-- Create Configurable Class --" << std::endl;
        std::cout << "===============" << std::endl;
        TestOmClassCreateConfigurable();
        std::cout << std::endl;

        std::cout << "-- Create Runtime Class --" << std::endl;
        std::cout << "===============" << std::endl;
        TestOmClassCreateRuntime();
        std::cout << std::endl;

        std::cout << "@@ CCB CREATE OBJECT @@" << std::endl;
        std::cout << "===============" << std::endl;
        TestOmCcbCreate();
        std::cout << std::endl;

        std::cout << "@@ Hold CCB CREATE OBJECT @@" << std::endl;
        std::cout << "===============" << std::endl;
        HoldValueCcbCreate();
        std::cout << std::endl;
        break;

      case 'i':
        std::cout << "Test IMM OI interfaces\n";
        std::cout << "===============" << std::endl;
        std::cout << "-- Create & Update Runtime Object --" << std::endl;
        TestOiRuntimeObject();
        std::cout << std::endl;
        break;

      case 'h':
        usage();
        break;

      case '?':
      default:
        fprintf(stderr, "Try -h for more information.\n");
        break;
    }
  }
  return 0;
}
