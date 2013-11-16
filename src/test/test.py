#!/usr/bin/python
from subprocess import CalledProcessError
from os import makedirs
import shlex, subprocess, shutil
import sys
import os
import unittest
import filecmp
import time

CMDFS = '../cmdfs'
SOURCE = "source"
DEST = "dest"
CACHE='cache'

shortcontent = "123456789" 


def rmf(root):
    if os.path.isdir(root):
        shutil.rmtree(root)

    
def setContents(file,contents):
        s1 = open(file,"w")
        s1.write(contents)
        s1.close

    

class TestCmdfs(unittest.TestCase):
    
    def unmount(self,dest):
        for i in range(0,3):
            try:
                subprocess.check_call( ["fusermount","-u",dest])#,stderr=open("/dev/null","w"))
            except CalledProcessError:
                print "Retrying unmount..."
                time.sleep(5)
            finally:
                return
        self.fail('Unmount failed')
        
        
    def mount(self,source,dest,options):
        if not 'cache-dir' in options:
            options['cache-dir']=self.cache
        for i in range(0,2):
            try:
                subprocess.check_call( [self.testDir+"/"+CMDFS,source , dest, "-o"+",".join( ["%s=%s" % (k,v) for (k,v) in options.iteritems() if v]+[k for (k,v) in options.iteritems() if not v])])
                break
            except CalledProcessError:
                self.unmount(dest)
        time.sleep(2) # empirically needs this to settle down, otherwise you get weird file stat errors (on non-fuse file access!!?)
        return (source+'/',dest+'/')

    def assertFileContentsEqual(self,file,other,msg):
 							f = open(file)   				
 							self.assertEqual(f.read(),other,msg)
 							f.close()
 							
    def assertFilesEqual(self,file,other,msg):
        self.assertTrue(filecmp.cmp(file, other) )
        
    def setUp(self):
        self.testDir = os.path.dirname(__file__)
        name = "%s/test-run/%d/%s" % (self.testDir,os.getpid(),self.id());
        rmf(name)
        makedirs(name)
        self.source = "%s/%s" % (name,SOURCE)        
        self.dest = "%s/%s" % (name,DEST)
        self.cache = "%s/%s" % (name,CACHE)
        os.makedirs(self.source)
        os.makedirs(self.dest)
        os.makedirs(self.cache)

    def tearDown(self):
        self.unmount(self.dest)

    def test_simple(self): 
        (s,d) = self.mount( self.source, self.dest, { 'path-re' : '.*' })
        for t in range(0,1000):
            n = 'test%d' % t
            setContents(s+n,shortcontent)
            self.assertFileContentsEqual( d+n,shortcontent,'file content')
        
    def test_simpleCommand(self): 
        (s,d) = self.mount( self.source, self.dest, { 'path-re' : '.*', 'command': 'echo -n "expected"' })
        setContents(s+'test',shortcontent)
        self.assertFileContentsEqual( d+'test','expected','simple command')

    def test_tokenCommand(self): 
        (s,d) = self.mount( self.source, self.dest, { 'path-re' : '.*', 'command': 'echo -n "%f"' })
        setContents(s+'test',shortcontent)
        self.assertFileContentsEqual( d+'test',os.path.abspath(s+'test'),'token replace command')

    def test_filterCommand(self): 
        (s,d) = self.mount( self.source, self.dest, { 'path-re' : '.*', 'command': 'wc -w' })
        setContents(s+'test',"one two three")
        self.assertFileContentsEqual(d+'test','3\n','filter command')

    def test_bigFile(self): 
        (s,d) = self.mount( self.source, self.dest, { 'path-re' : '.*', 'command': 'dd' })
        for t in range(0,100):
            n = 'big%d' % t
            bf = open(s+n,"w")
            for i in range(0,10000):
                bf.write('This is line %s\n' % i)
            bf.close()
#            print str(os.stat(s+n).st_size)
            try:
                self.assertFilesEqual(d+n,s+n,'big file compare')
            except AssertionError:
                raise;

    def test_subdirs(self):
        (s,d) = self.mount( self.source, self.dest, { 'path-re' : '.*' })
        p = 'sub1/sub2';
        os.makedirs(s+p)
        setContents(s+p+'/test',shortcontent)
        self.assertFileContentsEqual( d+p+'/test',shortcontent,'file content in subdir')
        
    def test_delete(self):
        (s,d) = self.mount( self.source, self.dest, { 'entry_timeout' : '0', 'path-re' : '.*' })
        setContents(s+'test',shortcontent)
        self.assertTrue(os.path.isfile(d+'test'))
        os.remove(s+'test')
        self.assertFalse(os.path.isfile(d+'test'))

    def test_path_re(self):
        (s,d) = self.mount( self.source, self.dest, { 'path-re' : '.*/yes/.*\.please' })
        setContents(s+'test.please',shortcontent)
        os.mkdir(s+'yes');
        setContents(s+'yes/test',shortcontent)
        setContents(s+'yes/test.please',shortcontent)
        self.assertFalse(os.path.isfile(d+'test.please'))
        self.assertFalse(os.path.isfile(d+'yes/test'))
        self.assertTrue(os.path.isfile(d+'yes/test.please'))

    def test_extension(self):
        (s,d) = self.mount( self.source, self.dest, { 'extension' : 'one;two' })
        setContents(s+'test.one',shortcontent)
        setContents(s+'test.two',shortcontent)
        setContents(s+'test.three',shortcontent)
        self.assertTrue(os.path.isfile(d+'test.one'))
        self.assertTrue(os.path.isfile(d+'test.two'))
        self.assertFalse(os.path.isfile(d+'test.three'))

    def test_mime_re(self):
        (s,d) = self.mount( self.source, self.dest, { 'mime-re' : 'image/.*' })
        shutil.copyfile(self.testDir+'/test.jpg', s+"testjpgnoext")
        self.assertTrue(os.path.isfile(d+'testjpgnoext'))
        setContents(s+'notreally.jpg',shortcontent)
        self.assertFalse(os.path.isfile(d+'notreally.jpg'))

    def test_link_thru(self):
        (s,d) = self.mount( self.source, self.dest, { 'link-thru' : None, 'path-re' : 'notalink' })
        setContents(s+'link1',shortcontent)
        self.assertTrue(os.path.islink(d+'link1'))
        self.assertFileContentsEqual(d+'link1',shortcontent,'link to correct file')
        setContents(s+'notalink',shortcontent)
        self.assertFalse(os.path.islink(d+'notalink'))
        self.assertFileContentsEqual(d+'notalink',shortcontent,'cmd generated file')

    def test_cache_expiry(self):
        expiry = 2;
        (s,d) = self.mount( self.source, self.dest, { 'cache-expiry' : str(expiry), 'path-re' : '.*', 'command': 'date +%s' })
        setContents(s+'file1','ignored')
        start = time.time()
        total = 0
        cnt = 0
        while time.time()-start < 60:
            total += time.time()-int(open(d+'file1').read())
            cnt+=1
            time.sleep(1)
        mean = total/cnt
        self.assertTrue( mean > expiry/2, 'mean age > half expiry age'  ) 
        self.assertTrue( mean < expiry*2,  'mean age < twice expiry age' )
        
    def test_hide_empty_dirs(self):
        (s,d) = self.mount( self.source, self.dest, { 'hide-empty-dirs' : None, 'path-re' : '.*/visiblefile' })
        os.makedirs(s+'level1')
        os.makedirs(s+'level1/level2')
        setContents(s+'level1/level2/notvisible',shortcontent)
        self.assertFalse(os.path.isdir(d+'level1'),'invisible dir - no visible files in any descendant')
        self.assertFalse(os.path.isdir(d+'level1/level2'),'invisible dir - no visible files in any descendant')
        self.assertFalse(os.path.isfile(d+'level1/level2/notvisible'),'no match')
        for (path,dirs,filenames) in os.walk(d):
            self.assertFalse(dirs,"Expected no sub dirs")
            self.assertFalse(filenames,"Expected no files")
        setContents(s+'level1/level2/visiblefile',shortcontent)
        self.assertTrue(os.path.isdir(d+'level1'),'invisible dir - a visible files in a descendant')
        self.assertTrue(os.path.isdir(d+'level1/level2'),'invisible dir - a visible file in a descendant')
        self.assertFalse(os.path.isfile(d+'level1/level2/notvisible'),'no match')
        self.assertTrue(os.path.isfile(d+'level1/level2/visiblefile'),'match')
        for (path,dirs,filenames) in os.walk(d):
            if dirs:
                self.assertTrue('level1' in dirs or 'level2' in dirs,"sub dirs")
            else:
                self.assertTrue('visiblefile' in filenames,"matching file")
                self.assertFalse('notvisible' in filenames,"no invisible file")
        
    def test_stat_pass_thru(self): 
        (s,d) = self.mount( self.source, self.dest, { 'stat-pass-thru' : None, 'path-re' : '.*', 'command': 'echo -n "abc"' })
        setContents(s+'test',shortcontent)
        st = os.stat(d+'test')
        self.assertEqual(st.st_size, len(shortcontent),'file should not have been cached')
        self.assertFileContentsEqual( d+'test','abc','file content') # will now cache
        st = os.stat(d+'test')
        self.assertEqual(st.st_size, len('abc'),'file should now be cached and stat should see length with command applied')
      
        
        
      
if __name__ == '__main__':
    unittest.main()


     
