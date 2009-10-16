/*-----------------------------------------------------------------------------
  NeXus - Neutron & X-ray Common Data Format
   
  Utility to convert a NeXus file into HDF4/HDF5/XML/...
 
  Author: Freddie Akeroyd
 
  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2 of the License, or (at your option) any later version.
 
  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.
 
  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 
  For further information, see <http://www.neutron.anl.gov/NeXus/>
 
 $Id: nxconvert.c 991 2008-03-19 19:30:03Z Freddie Akeroyd $
-----------------------------------------------------------------------------*/

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

#include "napi.h"
#include "napiconfig.h"
#include "nxconvert_common.h"
#include "tclap/CmdLine.h"

using namespace TCLAP;
using std::cout;
using std::cerr;
using std::endl;
using std::string;
using std::vector;

#if HAVE_SYS_WAIT_H
# include <sys/wait.h>
#endif
#ifndef WEXITSTATUS
# define WEXITSTATUS(stat_val) ((unsigned)(stat_val) >> 8)
#endif
#ifndef WIFEXITED
# define WIFEXITED(stat_val) (((stat_val) & 255) == 0)
#endif
#if HAVE_MKSTEMP
#else
static int mkstemp(char* template)
{
    if (mktemp(template) != NULL)
    {
	return open(template, O_RDWR | O_CREAT, 0600);
    }
    else
    {
	return -1;
    }
}
#endif

#ifdef _WIN32
#define NULL_DEVICE "NUL"
#define DIR_SEPARATOR "\\"
#define TMP_DIR getenv("TEMP")
#else
#define NULL_DEVICE "/dev/null"
#define DIR_SEPARATOR "/"
#define TMP_DIR P_tmpdir
#endif /* _WIN32 */

static const char* definition_name = NULL;
static const char* definition_version = NEXUS_SCHEMA_VERSION;
static const char* schema_path = ".";

static int url_encode(char c, FILE* f)
{
	switch(c)
	{
	    case '+':
	    case '&':
	    case '?':
	    case '$':
	    case ',':
	    case '/':
	    case ':':
	    case '=':
	    case ';':
	    case '@':
	    case '%':
	    case '#':
	        fprintf(f, "%%%02X", c);
	  	break;

	    default:
		fputc(c, f);
		break;
	}
	return 0;
}

#define NXVALIDATE_ERROR_EXIT	exit(1)


static int quiet = 0;

static int convertNXS(const string& nxsfile_in, string& nxsfile_out) 
{
   const char *inFile = nxsfile_in.c_str();
   char outFile[256];
   int fd;

   sprintf(outFile, "%s%s%s.XXXXXX", TMP_DIR, DIR_SEPARATOR, inFile);
   if ( (fd = mkstemp(outFile)) == -1 )
   {
     cerr << "error making temporary directory\n" << endl;
        return 1;
   }
   close(fd);
   if (!quiet) {
     cout << "* Writing tempfile " << outFile << endl;
   }
   nxsfile_out = outFile;
   if (convert_file(NX_DEFINITION, inFile, NXACC_READ, outFile,
                    NXACC_CREATEXML, definition_name) != NX_OK)
   {
     cerr << "* Error converting file " << nxsfile_in
          << " to definiton XML format";
     return 1;
   }
   return 0;
}

static int validate(const string& nxsfile, const char* definition_name, const char* definition_version, const int keep_temps, int use_web) 
{
   char command[512], outFile2[512], *strPtr;
   const char* cStrPtr;
   string extra_xmllint_args;
   int ret, opt, c, i, fd;
   FILE *fIn, *fOut2;

   extra_xmllint_args.append(" --nowarning");
   if (!quiet) {
     cout << "* Validating " << nxsfile << " using definition "
          << (definition_name != NULL ? definition_name : "<default>") << " for all NXentry" << endl;
   }
   string nxsfile_xml;
   if (convertNXS(nxsfile, nxsfile_xml) != 0) {
     return 1;
   }

   if (use_web == 0)
   {
       sprintf(command, "xmllint %s --noout --path \"%s\" --schema \"%s.xsd\" \"%s\" %s", 
         extra_xmllint_args.c_str(), schema_path, NEXUS_SCHEMA_BASE, nxsfile_xml.c_str(), (quiet ? "> " NULL_DEVICE " 2>&1" : ""));
       if (!quiet) {
         cout << "* Validating using locally installed \"xmllint\" program"
              << endl;
         cout << command << endl;
       }
       ret = system(command);
       if (ret != -1 && WIFEXITED(ret))
       {
	    if (!keep_temps)
	    {
		remove(nxsfile_xml.c_str());
	    }
	    if (WEXITSTATUS(ret) != 0)
	    {
              cout << "* Validation with \"xmllint\" of " << nxsfile
                   << " failed" << endl;
              cout << "* If the program was unable to find all the required "
                   << "schema from the web, check your \"http_proxy\" "
                   << "environment variable" << endl;
              NXVALIDATE_ERROR_EXIT;
	    }
	    else
	    {
	        if (!quiet) {
                  cout << "* Validation with \"xmllint\" of " << nxsfile 
                       << " OK" << endl;
                }
	        return 0;
	    }
       }
       cout << "* Unable to find \"xmllint\" to validate file - will try "
            << "web validation" << endl;
       cout << "* \"xmllint\" is installed as part of libxml2 from "
            << "xmlsoft.org" << endl;
       NXVALIDATE_ERROR_EXIT;
   }
   sprintf(outFile2, "%s%s%s.XXXXXX", TMP_DIR, DIR_SEPARATOR, nxsfile.c_str());
   if ( (fd = mkstemp(outFile2)) == -1 )
   {
     cerr << "Failed to make the temporary file " << outFile2 << endl;
     NXVALIDATE_ERROR_EXIT;
   }
   close(fd);
   fOut2 = fopen(outFile2, "wt");
   fIn = fopen(nxsfile_xml.c_str(), "rt");
   fprintf(fOut2, "file_name=");
   for(cStrPtr = nxsfile_xml.c_str(); *cStrPtr != '\0'; ++cStrPtr)
   {
	url_encode(*cStrPtr, fOut2);
   }
   fprintf(fOut2, "&definition_name=");
   for(cStrPtr = definition_name; cStrPtr != NULL && *cStrPtr != '\0'; ++cStrPtr)
   {
	url_encode(*cStrPtr, fOut2);
   }
   fprintf(fOut2, "&definition_version=");
   for(cStrPtr = definition_version; *cStrPtr != '\0'; ++cStrPtr)
   {
	url_encode(*cStrPtr, fOut2);
   }
   fprintf(fOut2, "&file_data=");
   while( (c = fgetc(fIn)) != EOF )
   {
	url_encode(c, fOut2);
   }
   fclose(fIn);
   fclose(fOut2);
   sprintf(command, "wget --quiet -O %s --post-file=\"%s\" http://definition.nexusformat.org/dovalidate/run", (quiet ? NULL_DEVICE : "-"), outFile2);
   if (!quiet) {
     cout << "* Validating via http://definition.nexusformat.org using "
          << "\"wget\"" << endl;
     cout << command << endl;
   }
   ret = system(command);
   if (!keep_temps)
   {
	remove(nxsfile_xml.c_str());
	remove(outFile2);
   }
   if (ret == -1)
   {
        cout << "* Unable to find \"wget\" to validate the file " << nxsfile
          << " over the web" << endl;
	NXVALIDATE_ERROR_EXIT;
   }

   if (WIFEXITED(ret) && (WEXITSTATUS(ret) != 0))
   {
        cerr << "* Validation via http://definition.nexusformat.org/ of "
          << nxsfile << " failed" << endl;
        cerr << "* If \"wget\" was unable to load the schema files from the "
          << "web, check your \"http_proxy\" environment variable" << endl;
	NXVALIDATE_ERROR_EXIT;
   }
   else if (!quiet)
   {
        cout << "* Validation via http://definition.nexusformat.org/ of "
          << nxsfile << " OK" << endl;
   }
   return 0;
}

int main(int argc, char *argv[])
{
   char inFile[256];
   char *strPtr;
   int use_web = 0;
   int keep_temps = 0;
   int ignore_definition = 0;

   // set up the command line arguments
   CmdLine cmd("Validate a NeXus file", ' ', "1.1.0");

   std::ostringstream temp;
   temp << "Use specified definiton for NXentry (default: read from <definition> field in NXentry )";
   ValueArg<string> definition_name_arg("d", "def", temp.str(), false, 
                                        "", "definition", cmd);
   temp.str("");
   temp << "specify schema version (default: " << definition_version << ")";
   ValueArg<string> definition_version_arg("", "defver", temp.str(), false, 
                                           definition_version, "version", cmd);

   SwitchArg quiet_arg("q", "quiet", "Turn on quiet mode (only report errors)",
                       false, cmd);
   SwitchArg keep_arg("k", "keep", "Keep temporary files",
                       false, cmd);
   SwitchArg nodef_arg("n", "nodef", "Ignore <definition> field in NXentry",
                       false, cmd);

   temp.str("");
   temp << "Specify path to additional schema files (default: " << schema_path << ")";
   ValueArg<string> schema_path_arg("p", "path", temp.str(), false, 
                                           schema_path, "path", cmd);

   temp.str("");
   temp << "Send file to NeXus web site (using wget) for validation "
        << "(default: try to run \"xmllint\" program locally";
   SwitchArg send_arg("w", "web", temp.str(), false, cmd);
   
   string sinfile;
   UnlabeledMultiArg<string> infiles_arg("filename",
                                         "Name of a file to be viewed",
                                         "filename",cmd);

   // parse the command line and turn it into variables
   cmd.parse(argc, argv);
   definition_name = definition_name_arg.getValue().c_str();
   if (definition_name[0] == '\0')
   {
      definition_name = NULL;
   }
   if (quiet_arg.getValue()) {
     quiet = 1;
   }
   if (send_arg.getValue()) {
     use_web = 1;
   }
   if (keep_arg.getValue()) {
     keep_temps = 1;
   }
   if (nodef_arg.getValue()) {
     ignore_definition = 1;
   }
   if (ignore_definition) {
      definition_name = "NXentry";
   }
   definition_version = definition_version_arg.getValue().c_str();
   schema_path = schema_path_arg.getValue().c_str();
   vector<string> infiles = infiles_arg.getValue();
   if (infiles.empty()) {
      printf ("Give name of input NeXus file : ");
      fgets (inFile, sizeof(inFile), stdin);
      if ((strPtr = strchr(inFile, '\n')) != NULL) { *strPtr = '\0'; }
      infiles.push_back(string(inFile));
   }

   // do the work
   int result = 0;
   for (size_t i = 0; i < infiles.size(); i++) {
     result += validate(infiles[i], definition_name, definition_version,
                        keep_temps, use_web);
   }
   return result;
}
