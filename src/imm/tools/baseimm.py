#      -*- OpenSAF  -*-
#
# (C) Copyright 2009 The OpenSAF Foundation
# (C) Copyright Ericsson AB 2015, 2016, 2017. All rights reserved.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
# or FITNESS FOR A PARTICULAR PURPOSE. This file and program are licensed
# under the GNU Lesser General Public License Version 2.1, February 1999.
# The complete license can be accessed from the following location:
# http://opensource.org/licenses/lgpl-license.php
# See the Copying file included with the OpenSAF distribution for full
# licensing terms.
#
# Author(s): Ericsson AB
#
""" Common utils to manipulate immxml files """
from __future__ import print_function
import os
import sys
import glob
import xml.dom.minidom
from subprocess import call


class BaseOptions(object):
    """ Base options for immxml tools """
    traceOn = False

    @staticmethod
    def print_option_settings():
        return 'Options:\n traceOn:%s\n' % BaseOptions.traceOn


class BaseImmDocument(object):
    """ Base class with common methods to manipulate IMM XML documents """
    imm_content_element_name = "imm:IMM-contents"
    imm_content_no_namespace_spec = "<imm:IMM-contents>"
    imm_content_with_namespace_spec = \
        "<imm:IMM-contents xmlns:imm=\"http://www.saforum.org/IMMSchema\" " \
        "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" " \
        "xsi:noNamespaceSchemaLocation=\"SAI-AIS-IMM-XSD-A.02.13.xsd\">"

    def verify_input_xml_document_file_is_parsable(self, file_name):
        """ Helper function to identify a problem where python/minidom fails to
        parse some imm.xml files where the imm namespace is missing (for ex,
        the output of the immdump tool)
        """
        if os.stat(file_name).st_size == 0:
            abort_script("The input file %s is empty!", file_name)

        doc = open(file_name)
        for _line in doc:
            line = _line
            string = line.replace(self.imm_content_no_namespace_spec,
                                  self.imm_content_with_namespace_spec)
            if len(string) != len(line):
                # There was a substitution....file will not be possible
                # to parse....
                abort_script("The input file lacks namespace specification in "
                             "<imm:IMM-contents> tag")
            elif line.find(self.imm_content_element_name) != -1:
                # Assume file is ok w.r.t namespace
                return

    @staticmethod
    def validate_xml_file_with_schema(file_name, schema_file_name):
        """ Validate xml file against its schema """
        trace("")
        trace("Validate xml file: %s", file_name)

        # Validate the result file with xmllint
        command = "/usr/bin/xmllint --noout"+" --schema " + schema_file_name \
                  + " " + file_name
        print_info_stderr("XML Schema validation (using xmllint):")
        trace("Validate command: %s", command)
        ret_code = call(command, shell=True)
        if ret_code != 0:
            return ret_code

        trace("Successfully validated xml document file: %s", file_name)
        return 0

    @staticmethod
    def format_xml_file_with_xmllint(in_file_name, out_file_name):
        """ Format xml file with xmllint """
        trace("format_xml_file_with_xmllint() prettify xml file: %s",
              in_file_name)

        # "prettify" the result file with xmllint
        # (due to inadequate python/minidom prettify functionality)
        command = "/bin/sh -c 'XMLLINT_INDENT=\"" + "\t" + \
                  "\"; export XMLLINT_INDENT; /usr/bin/xmllint --format " + \
                  in_file_name + " --output " + out_file_name + "'"
        trace("Formatting command: %s", command)
        ret_code = call(command, shell=True)
        # if ret_code != 0:
        #     return ret_code
        #     validate_failed("Failed to validate input file: %s", file_name)
        return ret_code

    @staticmethod
    def print_to_stdout_and_remove_tmp_file(temp_file):
        """ Print the temp file content to stdout and remove the temp file """
        # Print the tmp file to the standard output
        trace("Print the tmp file to the standard output: %s", temp_file)
        tmp_file = open(temp_file)
        while True:
            line = tmp_file.readline()
            if not line:
                break
            print (line, end=" ")  # Print on the same line
        tmp_file.close()
        # Remove temp file
        trace("Delete the stdout tmp file: %s", temp_file)
        os.remove(temp_file)

    def open_xml_document_file_and_check_namespace(self, file_name):
        """ Helper function to handle a problem where python/minidom fails to
        parse some imm.xml files where the imm namespace is missing (for ex,
        the output of the immdump tool).
        *** This method is currently NOT used ****
        """
        doc = open(file_name)
        str_list = []
        imm_contents_tag_found = False
        imm_contents_tag_replaced = False
        for _line in doc:
            line = _line
            if not imm_contents_tag_found:
                string = line.replace(self.imm_content_no_namespace_spec,
                                      self.imm_content_with_namespace_spec)
                if len(string) != len(line):
                    line = string
                    imm_contents_tag_found = True
                    # imm_contents_tag_replaced = True
                elif line.find(self.imm_content_element_name) != -1:
                    imm_contents_tag_found = True

            str_list.append(line)

        xml_str = ' '.join(str_list)

        if Options.schemaFilename is not None:
            if imm_contents_tag_replaced:
                print_info_stderr("Cannot validate input file '%s' with "
                                  "schema file because of missing namespace "
                                  "specification in element "
                                  "<imm:IMM-contents>. \nProceeding with "
                                  "processing of modified input data!",
                                  file_name)
            else:
                if self.validate_xml_file(file_name) != 0:
                    abort_script("Failed to validate the input file: %s",
                                 file_name)

        return xml.dom.minidom.parseString(xml_str)

    @staticmethod
    def open_xml_document_and_strip(file_name):
        """ Helper function to remove some whitespace which we do not want to
        have in result of to_pretty_xml
        *** This method is currently NOT used ***
        """
        doc = open(file_name)
        str_list = []
        for line in doc:
            # string = line.rstrip().lstrip()
            string = line.strip('\t\n')
            if len(string) < 5:
                trace("short line *%s*", string)
            str_list.append(string)

        xml_str = ' '.join(str_list)
        return xml.dom.minidom.parseString(xml_str)

# End of BaseImmDocument class


def trace(*args):
    """ Print traces to stderr if trace option is enabled """
    if BaseOptions.traceOn:
        printf_args = []
        for i in range(1, len(args)):
            printf_args.append(args[i])

        format_str = "TRACE:\t" + args[0]
        print (format_str % tuple(printf_args), file=sys.stderr)


def retrieve_file_names(args):
    """ Retrieve file names """
    trace("Before glob args: %s", args)
    # Wildcard expansion for WIN support, however not tested....
    file_list = glob.glob(args[0])
    trace("After glob file list: %s length: %d", file_list, len(file_list))

    if len(file_list) < 2 and len(args) >= 1:
        file_list = args

    trace("Final file list: %s length: %d", file_list, len(file_list))
    return file_list


def print_info_stderr(*args):
    """ Print info to stderr """
    printf_args = []
    for i in range(1, len(args)):
        printf_args.append(args[i])

    format_str = args[0]
    print (format_str % tuple(printf_args), file=sys.stderr)


def abort_script(*args):
    """ Abort the script and print info to stderr """
    printf_args = []
    for i in range(1, len(args)):
        printf_args.append(args[i])

    format_str = "\nAborting script: " + args[0]
    print (format_str % tuple(printf_args), file=sys.stderr)
    sys.exit(2)


def exit_script(*args):
    """ Exit the script and print info to stderr """
    printf_args = []
    for i in range(1, len(args)):
        printf_args.append(args[i])

    format_str = "\n" + args[0]
    print (format_str % tuple(printf_args), file=sys.stderr)
    sys.exit(0)


def verify_input_file_read_access(file_name):
    """ Verify if input file has read access """
    if not os.access(file_name, os.R_OK):
        abort_script("Cannot access input file: %s", file_name)


# An attempt to fix minidom pretty print functionality
# see http://ronrothman.com/public/leftbraned/ \
# xml-dom-minidom-toprettyxml-and-silly-whitespace/
# However the result is still not nice.
# (have replaced it with xmllint formatting)
# This way the method gets replaced, worked when called from main()
# Attempt to replace minidom's function with a "hacked" version to get decent
# prettyXml, however it does not look nice anyway.
# xml.dom.minidom.Element.writexml = fixed_writexml
def fixed_writexml(self, writer, indent="", add_indent="", new_line=""):
    """ Customized pretty xml print function """
    # indent = current indentation
    # add_indent = indentation to add to higher levels
    # new_line = newline string
    writer.write(indent + "<" + self.tagName)

    attrs = self._get_attributes()
    a_names = attrs.keys()
    a_names.sort()

    for a_name in a_names:
        writer.write(" %s=\"" % a_name)
        xml.dom.minidom._write_data(writer, attrs[a_name].value)
        writer.write("\"")
    if self.childNodes:
        if len(self.childNodes) == 1 \
          and self.childNodes[0].nodeType == xml.dom.minidom.Node.TEXT_NODE:
            writer.write(">")
            self.childNodes[0].writexml(writer, "", "", "")
            writer.write("</%s>%s" % (self.tagName, new_line))
            return
        writer.write(">%s" % new_line)
        for node in self.childNodes:
            node.writexml(writer, indent + add_indent, add_indent, new_line)
        writer.write("%s</%s>%s" % (indent, self.tagName, new_line))
    else:
        writer.write("/>%s" % new_line)
