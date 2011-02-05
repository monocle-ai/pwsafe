/*
* Copyright (c) 2003-2011 Rony Shapiro <ronys@users.sourceforge.net>.
* All rights reserved. Use of the code is allowed under the
* Artistic License 2.0 terms, as specified in the LICENSE file
* distributed with this code, or available from
* http://www.opensource.org/licenses/artistic-license-2.0.php
*/

#include <iostream>
#include <unistd.h>
#include <getopt.h>

#include "PWScore.h"
#include "os/file.h"
#include "core/UTF8Conv.h"

#include <termios.h>

using namespace std;

static struct termios oldTermioFlags;

static void usage(char *pname)
{
  cerr << "Usage: " << pname << " safe --imp[=file] --text|--xml" << endl
       << "\t safe --exp[=file] --text|--xml" << endl;
}

static const char *status_text(int status)
{
  switch (status) {
  case PWScore::SUCCESS: return "SUCCESS";
  case PWScore::FAILURE: return "FAILURE";
  case PWScore::CANT_OPEN_FILE: return "CANT_OPEN_FILE";
  case PWScore::USER_CANCEL: return "USER_CANCEL";
  case PWScore::WRONG_PASSWORD: return "WRONG_PASSWORD";
  case PWScore::BAD_DIGEST: return "BAD_DIGEST";
  case PWScore::UNKNOWN_VERSION: return "UNKNOWN_VERSION";
  case PWScore::NOT_SUCCESS: return "NOT_SUCCESS";
  case PWScore::ALREADY_OPEN: return "ALREADY_OPEN";
  case PWScore::INVALID_FORMAT: return "INVALID_FORMAT";
  case PWScore::USER_EXIT: return "USER_EXIT";
  case PWScore::XML_FAILED_VALIDATION: return "XML_FAILED_VALIDATION";
  case PWScore::XML_FAILED_IMPORT: return "XML_FAILED_IMPORT";
  case PWScore::LIMIT_REACHED: return "LIMIT_REACHED";
  case PWScore::UNIMPLEMENTED: return "UNIMPLEMENTED";
  default: return "UNKNOWN !!!";
  }
}

struct UserArgs {
  UserArgs() : ImpExp(Unset), Format(Unknown) {}
  StringX safe, fname;
  enum {Unset, Import, Export} ImpExp;
  enum {Unknown, XML, Text} Format;
};

bool parseArgs(int argc, char *argv[], UserArgs &ua)
{
  if (argc != 4 && argc != 5)
    return false;
  CUTF8Conv conv;
  if (!conv.FromUTF8((const unsigned char *)argv[1], strlen(argv[1]),
                     ua.safe)) {
    cerr << "Could not convert filename " << argv[1] << " to StringX" << endl;
    exit(2);
  }
  while (1) {
    int option_index = 0;
    static struct option long_options[] = {
      // name, has_arg, flag, val
      {"import", optional_argument, 0, 'i'},
      {"export", optional_argument, 0, 'e'},
      {"text", no_argument, 0, 't'},
      {"xml", no_argument, 0, 'x'},
      {0, 0, 0, 0}
    };

    int c = getopt_long(argc-1, argv+1, "i::e::tx",
                        long_options, &option_index);
    if (c == -1)
      break;

    switch (c) {
    case 'i':
      if (ua.ImpExp == UserArgs::Unset)
        ua.ImpExp = UserArgs::Import;
      else
        return false;
      if (optarg) {
        if (!conv.FromUTF8((const unsigned char *)optarg, strlen(optarg),
                           ua.fname)) {
          cerr << "Could not convert filename "
               << optarg << " to StringX" << endl;
          exit(2);
        }
      }
      break;
    case 'e':
      if (ua.ImpExp == UserArgs::Unset)
        ua.ImpExp = UserArgs::Export;
      else
        return false;
      if (optarg) {
        if (!conv.FromUTF8((const unsigned char *)optarg, strlen(optarg),
                           ua.fname)) {
          cerr << "Could not convert filename "
               << optarg << " to StringX" << endl;
          exit(2);
        }
      }
      break;
    case 'x':
      if (ua.Format == UserArgs::Unknown)
        ua.Format = UserArgs::XML;
      else
        return false;
      break;
    case 't':
      if (ua.Format == UserArgs::Unknown)
        ua.Format = UserArgs::Text;
      else
        return false;
      break;

    default:
      cerr << "Unknown option: " << char(c) << endl;
      return false;
    }
    if (ua.fname.empty())
      ua.fname = (ua.Format == UserArgs::XML) ? L"file.xml" : L"file.txt";
  }
  return true;
}

void echoOff()
{
  struct termios nflags;
  tcgetattr(fileno(stdin), &oldTermioFlags);
  nflags = oldTermioFlags;
  nflags.c_lflag &= ~ECHO;
  nflags.c_lflag |= ECHONL;

  if (tcsetattr(fileno(stdin), TCSANOW, &nflags) != 0) {
    cerr << "Couldn't turn off echo\n";
  }
}

void echoOn()
{
  if (tcsetattr(fileno(stdin), TCSANOW, &oldTermioFlags) != 0) {
    cerr << "Couldn't restore echo\n";
  }
}

int main(int argc, char *argv[])
{
  UserArgs ua;
  if (!parseArgs(argc, argv, ua)) {
    usage(argv[0]);
    return 1;
  }

  PWScore core;
  if (!pws_os::FileExists(ua.safe.c_str())) {
    cerr << argv[1] << " - file not found" << endl;
    return 2;
  }
  wstring wpk;
  cout << "Enter Password: ";
  echoOff();
  wcin >> wpk;
  echoOn();
  StringX pk(wpk.c_str());

  int status;
  status = core.CheckPasskey(ua.safe, pk);
  if (status != PWScore::SUCCESS) {
    cout << "CheckPasskey returned: " << status_text(status) << endl;
    goto done;
  }
  {
    CUTF8Conv conv;
    const char *user = getlogin() != NULL ? getlogin() : "unknown";
    StringX locker;
    if (!conv.FromUTF8((const unsigned char *)user, strlen(user), locker)) {
      cerr << "Could not convert user " << user << " to StringX" << endl;
      return 2;
    }
    stringT lk(locker.c_str());
    if (!core.LockFile(ua.safe.c_str(), lk)) {
      wcout << L"Couldn't lock file " << ua.safe
            << L": locked by " << locker << endl;
      status = -1;
      goto done;
    }
  }
  status = core.ReadFile(ua.safe, pk);
  if (status != PWScore::SUCCESS) {
    cout << "ReadFile returned: " << status_text(status) << endl;
    goto done;
  }

  if (ua.ImpExp == UserArgs::Export) {
    CItemData::FieldBits all(~0L);
    int N;
    if (ua.Format == UserArgs::XML) {
      core.WriteXMLFile(ua.fname, all, L"", 0, 0, L' ', N);
    } else { // export text
      core.WritePlaintextFile(ua.fname, all, L"", 0, 0, L' ', N);
    }
  } else { // Import
    if (ua.Format == UserArgs::XML) {
    } else { // import text
    }
  }
 done:
  core.UnlockFile(ua.safe.c_str());
  return status;
}
