# -*- coding: utf-8 -*-
# Pretty-printers for KDE4.

# Copyright (C) 2009 Milian Wolff <mail@milianw.de>

# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

import gdb
import itertools
import re

class CursorPrinter:
    "Pretty Printer for KTextEditor::Cursor"

    def __init__(self, val):
        self.val = val

    def to_string(self):
        return "[%d, %d]" % (self.val['m_line'], self.val['m_column'])

class RangePrinter:
    "Pretty Printer for KTextEditor::Range"

    def __init__(self, val):
        self.val = val

    def to_string(self):
        return "[ (%d, %d) -> (%d, %d) ]" % (self.val['m_start']['m_line'], self.val['m_start']['m_column'],
                                             self.val['m_end']['m_line'], self.val['m_end']['m_column'])

def register_kde4_printers (obj):
    if obj == None:
        obj = gdb

    obj.pretty_printers.append (lookup_function)

def lookup_function (val):
    "Look-up and return a pretty-printer that can print val."

    # Get the type.
    type = val.type;

    # If it points to a reference, get the reference.
    if type.code == gdb.TYPE_CODE_REF:
        type = type.target ()

    # Get the unqualified type, stripped of typedefs.
    type = type.unqualified ().strip_typedefs ()

    # Get the type name.
    typename = type.tag
    if typename == None:
        return None

    # Iterate over local dictionary of types to determine
    # if a printer is registered for that type.  Return an
    # instantiation of the printer if found.
    for function in pretty_printers_dict:
        if function.search (typename):
            return pretty_printers_dict[function] (val)

    # Cannot find a pretty printer.  Return None.
    return None

def build_dictionary ():
    pretty_printers_dict[re.compile('^KTextEditor::Cursor$')] = lambda val: CursorPrinter(val)
    pretty_printers_dict[re.compile('^KTextEditor::Range$')] = lambda val: RangePrinter(val)


pretty_printers_dict = {}

build_dictionary ()
