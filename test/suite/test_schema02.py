#!/usr/bin/env python
#
# Copyright (c) 2008-2012 WiredTiger, Inc.
#
# This is free and unencumbered software released into the public domain.
#
# Anyone is free to copy, modify, publish, use, compile, sell, or
# distribute this software, either in source code form or as a compiled
# binary, for any purpose, commercial or non-commercial, and by any
# means.
#
# In jurisdictions that recognize copyright laws, the author or authors
# of this software dedicate any and all copyright interest in the
# software to the public domain. We make this dedication for the benefit
# of the public at large and to the detriment of our heirs and
# successors. We intend this dedication to be an overt act of
# relinquishment in perpetuity of all present and future rights to this
# software under copyright law.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR
# OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
# ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
# OTHER DEALINGS IN THE SOFTWARE.
#
# test_schema02.py
# 	Columns, column groups, indexes
#

import wiredtiger, wttest

class test_schema02(wttest.WiredTigerTestCase):
    """
    Test basic operations
    """
    nentries = 1000

    def expect_failure_primary(self, configstr):
        self.assertRaises(wiredtiger.WiredTigerError,
                          lambda:self.session.create("table:main", configstr))

    def expect_failure_colgroup(self, name, configstr):
        self.assertRaises(wiredtiger.WiredTigerError,
                          lambda:self.session.create("colgroup:" + name, configstr))

    def test_colgroup_after_failure(self):
        # bogus formats
        self.expect_failure_primary("key_format=Z,value_format=Y")

        # These should succeed
        self.session.create("table:main", "key_format=iS,value_format=SiSi,"
                            "columns=(ikey,Skey,S1,i2,S3,i4),colgroups=(c1,c2)")
        self.session.create("colgroup:main:c1", "columns=(S1,i2)")

    def test_colgroup_failures(self):
        # too many columns
        #### TEST FAILURE HERE ####
        self.expect_failure_primary("key_format=S,value_format=,columns=(a,b)")
        # Note: too few columns is allowed

        # expect this to work
        self.session.create("table:main", "key_format=iS,value_format=SiSi,"
                            "columns=(ikey,Skey,S1,i2,S3,i4),colgroups=(c1,c2)")

        # bad table name
        #### TEST FAILURE HERE ####
        self.expect_failure_colgroup("nomatch:c", "columns=(S1,i2)")
        # colgroup not declared in initial create
        self.expect_failure_colgroup("main:nomatch", "columns=(S1,i2)")
        # bad column
        self.expect_failure_colgroup("main:c1", "columns=(S1,i2,bad)")
        # no columns
        #### TEST FAILURE HERE ####
        #self.expect_failure_colgroup("main:c1", "columns=()")
        # key in a column group
        self.expect_failure_colgroup("main:c1", "columns=(ikey,S1,i2)")

        # expect this to work
        self.session.create("colgroup:main:c1", "columns=(S1,i2)")

        # colgroup not declared in initial create
        self.expect_failure_colgroup("main:c3", "columns=(S3,i4)")

        # missing columns
        self.expect_failure_colgroup("main:c2", "columns=(S1,i4)")

        # expect this to work
        #### TEST FAILURE HERE ####
        self.session.create("colgroup:main:c2", "columns=(S3,i4)")

        # expect these to work - each table name is a separate namespace
        self.session.create("table:main2", "key_format=iS,value_format=SiSi,"
                            "columns=(ikey,Skey,S1,i2,S3,i4),colgroups=(c1,c2)")
        self.session.create("colgroup:main2:c1", "columns=(S1,i2)")
        self.session.create("colgroup:main2:c2", "columns=(S3,i4)")

    def test_index(self):
        self.session.create("table:main", "key_format=iS,value_format=SiSi,"
                            "columns=(ikey,Skey,S1,i2,S3,i4),colgroups=(c1,c2)")

        # should be able to create colgroups before indices
        self.session.create("colgroup:main:c2", "columns=(S3,i4)")

        # should be able to create indices on all key combinations
        self.session.create("index:main:ikey", "columns=(ikey)")
        self.session.create("index:main:Skey", "columns=(Skey)")
        self.session.create("index:main:ikeySkey", "columns=(ikey,Skey)")
        self.session.create("index:main:Skeyikey", "columns=(Skey,ikey)")

        # should be able to create indices on all value combinations
        self.session.create("index:main:S1", "columns=(S1)")
        self.session.create("index:main:i2", "columns=(i2)")
        self.session.create("index:main:i2S1", "columns=(i2,S1)")
        self.session.create("index:main:S1i4", "columns=(S1,i4)")

        # somewhat nonsensical to repeat columns within an index, but allowed
        self.session.create("index:main:i4S3i4S1", "columns=(i4,S3,i4,S1)")

        # should be able to create colgroups after indices
        self.session.create("colgroup:main:c1", "columns=(S1,i2)")

        self.populate()

        # should be able to create indices after populating
        self.session.create("index:main:i2S1i4", "columns=(i2,S1,i4)")

        self.check_entries()

    def populate(self):
        cursor = self.session.open_cursor('table:main', None, None)
        for i in range(0, self.nentries):
            cursor.set_key(i, 'key' + str(i))
            square = i * i
            cube = square * i
            cursor.set_value('val' + str(square), square, 'val' + str(cube), cube)
            cursor.insert()
        cursor.close()
        
    def check_entries(self):
        cursor = self.session.open_cursor('table:main', None, None)
        # spot check via search
        n = self.nentries
        for i in (n / 5, 0, n - 1, n - 2, 1):
            cursor.set_key(i, 'key' + str(i))
            square = i * i
            cube = square * i
            cursor.search()
            (s1, i2, s3, i4) = cursor.get_values()
            self.assertEqual(s1, 'val' + str(square))
            self.assertEqual(i2, square)
            self.assertEqual(s3, 'val' + str(cube))
            self.assertEqual(i4, cube)

        i = 0
        # then check all via cursor
        cursor.reset()
        for ikey, skey, s1, i2, s3, i4 in cursor:
            square = i * i
            cube = square * i
            self.assertEqual(ikey, i)
            self.assertEqual(skey, 'key' + str(i))
            self.assertEqual(s1, 'val' + str(square))
            self.assertEqual(i2, square)
            self.assertEqual(s3, 'val' + str(cube))
            self.assertEqual(i4, cube)
            i += 1
        cursor.close()
        self.assertEqual(i, n)
        
    def test_colgroups(self):
        self.session.create("table:main", "key_format=iS,value_format=SiSi,"
                            "columns=(ikey,Skey,S1,i2,S3,i4),colgroups=(c1,c2)")
        self.session.create("colgroup:main:c1", "columns=(S1,i2)")
        self.session.create("colgroup:main:c2", "columns=(S3,i4)")
        self.populate()
        self.check_entries()


if __name__ == '__main__':
    wttest.run()