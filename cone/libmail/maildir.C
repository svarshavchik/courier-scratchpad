/*
** Copyright 2002-2004, S. Varshavchik.
**
** See COPYING for distribution information.
*/
#include "libmail_config.h"
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <fcntl.h>

#include "expungelist.H"
#include "misc.H"
#include "maildir.H"
#include "driver.H"
#include "search.H"
#include "copymessage.H"
#include "file.H"

#include "maildir/config.h"
#include "maildir/maildirmisc.h"
#include "maildir/maildirquota.h"
#include "rfc2045/rfc2045.h"

#include <fcntl.h>
#include <unistd.h>

#include <list>
#include <map>
#include <memory>
#include <utility>

using namespace std;

/////////////////////////////////////////////////////////////////////////////

LIBMAIL_START

static bool open_maildir(mail::account *&accountRet,
			 mail::account::openInfo &oi,
			 mail::callback &callback,
			 mail::callback::disconnect &disconnectCallback)
{
	if (oi.url.substr(0, 8) != "maildir:")
		return false;

	accountRet=new mail::maildir(disconnectCallback,
				     callback, oi.url.substr(8));
	return true;
}

static bool maildir_remote(string url, bool &flag)
{
	if (url.substr(0, 8) != "maildir:")
		return false;

	flag=false;
	return true;
}

driver maildir_driver= { &open_maildir, &maildir_remote };

LIBMAIL_END

/////////////////////////////////////////////////////////////////////////////

mail::maildir::maildirMessageInfo::maildirMessageInfo()
	: lastKnownFilename(""), changed(false)
{
}

mail::maildir::maildirMessageInfo::~maildirMessageInfo()
{
}

mail::maildir::maildir(mail::callback::disconnect &disconnect_callback,
		       mail::callback &callback,
		       string pathArg)
	: mail::account{disconnect_callback}
{
	if (init(callback, pathArg))
		callback.success("Mail folders opened");
}

mail::maildir::maildir(mail::callback::disconnect &disconnect_callback)
	: mail::account{disconnect_callback}
{
	// Used by pop3maildrop subclass.
}

bool mail::maildir::init(mail::callback &callback,
			 string pathArg)
{
	ispop3maildrop=false;

	if (pathArg.size() == 0)
		pathArg="Maildir";

	string h=mail::homedir();

	if (h.size() == 0)
	{
		callback.fail("Cannot find my home directory!");
		return false;
	}

	if (pathArg[0] != '/')
		pathArg= h + "/" + pathArg;

	static const char * const subdirs[]={"tmp","cur","new", 0};

	int i;

	for (i=0; subdirs[i]; i++)
	{
		struct stat stat_buf;

		string s= pathArg + "/" + subdirs[i] + "/.";

		if (stat(s.c_str(), &stat_buf) < 0)
		{
			s="Cannot open maildirAccount: ";
			s += strerror(errno);
			callback.fail(s);
			return false;
		}
	}

	path=pathArg;
	return true;
}

mail::maildir::~maildir()
{
	updateNotify(false);

	if (!calledDisconnected)
	{
		calledDisconnected=true;
		disconnect_callback.disconnected("");
	}

	index.clear();
}

void mail::maildir::resumed()
{
}

void mail::maildir::handler(vector<pollfd> &fds, int &ioTimeout)
{
	int fd;
	bool changed;
	int timeout;

	if (!watchFolder)
		return;

	if (watchStarting)
	{
		if (!watchFolderContents->started())
		{
			updateNotify(false);
			return;
		}

		watchStarting=false;
		updateFolderIndexInfo(NULL, false);
	}

	changed=watchFolderContents->check(fd, timeout);

	if (changed)
	{
		ioTimeout=0;

		MONITOR(mail::maildir);

		updateNotify(false);
		updateFolderIndexInfo(nullptr, false);

		if (!DESTROYED())
			updateNotify(true);
		updateFolderIndexInfo(nullptr, false);
		return;
	}

	timeout *= 1000;

	if (timeout < ioTimeout)
	{
		ioTimeout=timeout;
	}
	if (fd < 0)
		return;

	pollfd pfd;

	pfd.fd=fd;
	pfd.events=pollread;
	pfd.revents=0;
	fds.push_back(pfd);
}

void mail::maildir::logout(mail::callback &callback)
{
	updateNotify(false);

	if (!calledDisconnected)
	{
		calledDisconnected=true;
		disconnect_callback.disconnected("");
	}
	callback.success("OK");
}

//
// Callback collects filenames moved from new to cur on this maildirAccount scan
//
static void recent_callback_func(const char *c, void *vp)
{
	set<string> *setPtr=(set<string> *)vp;

	try {
		setPtr->insert( string(c));
	} catch (...) {
	}
}

// Update mail::messageInfo with the current message flags
// (read from the message's filename)
// Returns true if flags have been modified

bool mail::maildir::updateFlags(const char *filename,
				mail::messageInfo &info)
{
	bool flag;
	bool changed=false;

#define DOFLAG(name, NOT, theChar) \
	if ((flag= NOT !!maildir_hasflag(filename, theChar)) != info.name) \
		{ info.name=flag; changed=true; }
#define DOFLAG_EMPTY

	DOFLAG(draft, DOFLAG_EMPTY, 'D');
	DOFLAG(replied, DOFLAG_EMPTY, 'R');
	DOFLAG(marked, DOFLAG_EMPTY, 'F');
	DOFLAG(deleted, DOFLAG_EMPTY, 'T');
	DOFLAG(unread, !, 'S');

#undef DOFLAG
#undef DOFLAG_EMPTY

	return changed;
}

//
// Get the filename for message #n
//

string mail::maildir::getfilename(size_t i)
{
	string n("");

	if (i >= index.size())
		return n;

	auto dir=::maildir::name2dir(path, folderPath);

	n=::maildir::filename(dir, "", index[i].lastKnownFilename);

	return n;
}

void mail::maildir::checkNewMail(callback &callback)
{
	checkNewMail(&callback);
}

class mail::maildir::readKeywordHelper {

	mail::maildir *md;

	map<string, size_t> filenameMap;

	mail::keywords::hashtable<std::string> h;

	vector<mail::keywords::message<std::string>> kwArray;


public:
	readKeywordHelper(mail::maildir *mdArg);
	~readKeywordHelper();

	bool go(string maildir, bool &rc);
};


mail::maildir::readKeywordHelper::readKeywordHelper(mail::maildir *mdArg)
	: md(mdArg)
{
	size_t n=md->index.size();
	size_t i;

	kwArray.reserve(n);

	for (i=0; i<n; i++)
	{
		string f=md->index[i].lastKnownFilename;
		size_t sep=f.rfind(MDIRSEP[0]);

		if (sep != std::string::npos)
			f=f.substr(0, sep);

		filenameMap.insert(make_pair(f, i));
		kwArray.emplace_back(h, mail::keywords::list{}, f);
	}
}

bool mail::maildir::readKeywordHelper::go(string maildir, bool &rc)
{
	unique_ptr<::maildir::watch::lock> imap_lock;

	if (md->lockFolder)
	{
		imap_lock.reset(
			new ::maildir::watch::lock{
				*md->lockFolder
			}
		);
	}

	h.load(maildir,
	       []
	       (const string &filename)
	       {
		       return filename;
	       },
	       [&]
	       (const string &filename,
		mail::keywords::list &keywords)
	       {
		       auto i=filenameMap.find(filename);

		       if (i == filenameMap.end())
			       return false;

		       kwArray.at(i->second).keywords(h, keywords,
						      filename);
		       return true;
	       },
	       []
	       {
		       return false;
	       });

	for (size_t i=0; i<kwArray.size(); i++)
	{
		auto kw=kwArray[i].keywords();

		if (md->index[i].keywords.keywords() != kw)
		{
			md->index[i].keywords.keywords(
				md->keywordHashtable,
				kw
			);
			md->index[i].changed=true;
		}
	}

	rc=true;
	return false;
}

mail::maildir::readKeywordHelper::~readKeywordHelper()=default;

void mail::maildir::checkNewMail(callback *callback)
{
	if (folderPath.size() == 0)
	{
		if (callback)
			callback->success("OK");
		return;
	}

	set<string> recentMessages;
	vector<maildirMessageInfo> newIndex;

	string md;

	char *d=maildir_name2dir(path.c_str(), folderPath.c_str());

	if (!d)
	{
		callback->fail("Invalid folder");
		return;
	}

	try {
		md=d;
		free(d);
	} catch (...) {
		free(d);
		LIBMAIL_THROW(LIBMAIL_THROW_EMPTY);
	}

	// Move messages from new to cur

	maildir_getnew(md.c_str(), nullptr,
		       &recent_callback_func, &recentMessages);

	// Now, rescan the cur directory

	scan(folderPath, newIndex);

	// Now, mark messages from from new to cur as recent

	set<string> existingMessages;

	size_t i, n=index.size();

	for (i=0; i<n; i++)
		existingMessages.insert(index[i].uid);

	bool exists=false;

	for (i=0; i<newIndex.size(); i++)
	{
		newIndex[i].recent=
			recentMessages.count(newIndex[i].lastKnownFilename)>0;

		if (existingMessages.count(newIndex[i].uid) == 0)
		{
			exists=true;
			index.push_back(newIndex[i]);
		}
	}

	// Invoke callbacks to reflect deleted, or changed, mail

	MONITOR(mail::maildir);

	mail::expungeList removedList;
	list<size_t> changedList;
	while (n > 0)
	{
		--n;

		string messageFn=getfilename(n);

		if (messageFn.size() == 0)
		{
			index.erase(index.begin() + n);

			removedList << n;
			continue;
		}
		else
		{
			index[n].changed=
				updateFlags(messageFn.c_str(), index[n]);

			index[n].lastKnownFilename=
				strrchr(messageFn.c_str(), '/')+1;
		}
	}

	readKeywordHelper rkh(this);

	bool rc;

	while (rkh.go(md, rc))
		;

	if (!rc)
	{
		if (callback)
			callback->fail(strerror(errno));
		return;
	}

	n=index.size();

	while (n > 0)
	{
		--n;

		if (index[n].changed)
			changedList.insert(changedList.begin(), n);
	}


	removedList >> folderCallback;

	while (!DESTROYED() && !changedList.empty())
	{
		list<size_t>::iterator b=changedList.begin();

		size_t n= *b;

		changedList.erase(b);

		if (folderCallback)
			folderCallback->messageChanged(n);
	}

	if (!DESTROYED() && exists && folderCallback)
		folderCallback->newMessages();

	if (callback)
		callback->success("OK");
}




void mail::maildir::genericMarkRead(size_t messageNumber)
{
	if (messageNumber < index.size() && index[messageNumber].unread)
	{
		index[messageNumber].unread=false;
		updateFlags(messageNumber);
	}
}

bool mail::maildir::hasCapability(string capability)
{
	return false;
}

string mail::maildir::getCapability(string name)
{
	upper(name);

	if (name == LIBMAIL_SERVERTYPE)
	{
		return "maildir";
	}

	if (name == LIBMAIL_SERVERDESCR)
	{
		return "Local maildir";
	}

	return "";
}

void mail::maildir::readMessageAttributes(const vector<size_t> &messages,
					  MessageAttributes attributes,
					  mail::callback::message &callback)
{
	genericAttributes(this, this, messages, attributes, callback);
}

void mail::maildir::readMessageContent(const vector<size_t> &messages,
				       bool peek,
				       enum mail::readMode readType,
				       mail::callback::message &callback)
{
	genericReadMessageContent(this, this, messages, peek, readType,
				  callback);
}

void mail::maildir::readMessageContent(size_t messageNum,
				       bool peek,
				       const class mail::mimestruct &msginfo,
				       enum mail::readMode readType,
				       mail::callback::message &callback)
{
	genericReadMessageContent(this, this, messageNum, peek, msginfo,
				  readType, callback);
}

void mail::maildir::readMessageContentDecoded(size_t messageNum,
					      bool peek,
					      const mail::mimestruct &msginfo,
					      mail::callback::message
					      &callback)
{
	genericReadMessageContentDecoded(this, this, messageNum, peek,
					 msginfo, callback);
}

size_t mail::maildir::getFolderIndexSize()
{
	return index.size();
}

mail::messageInfo mail::maildir::getFolderIndexInfo(size_t msgNum)
{
	if (msgNum < index.size())
		return index[msgNum];

	return mail::messageInfo();
}

void mail::maildir::saveFolderIndexInfo(size_t msgNum,
					const mail::messageInfo &info,
					mail::callback &callback)
{
	if (msgNum >= index.size())
	{
		callback.success("OK");
		return;
	}

	mail::messageInfo &newFlags=index[msgNum];

#define DOFLAG(dummy, field, dummy2) \
		newFlags.field=info.field;

	LIBMAIL_MSGFLAGS;

#undef DOFLAG

	string errmsg="Message updated";

	if (!updateFlags(msgNum))
		errmsg="Folder opened in read-only mode.";

	callback.success(errmsg);
}

bool mail::maildir::updateFlags(size_t msgNum)
{
	bool changed=true;

	string messageFn=getfilename(msgNum);

	if (messageFn.size() > 0)
	{
		string s(strrchr(messageFn.c_str(), '/')+1);

		size_t i=s.find(MDIRSEP[0]);

		if (i != string::npos)
			s=s.substr(0, i);

		s += MDIRSEP "2,";

		s += getMaildirFlags(index[msgNum]);

		const char *fnP=messageFn.c_str();

		string newName=string(fnP, (const char *)strrchr(fnP, '/')+1)
			+ s;

		if (newName != messageFn)
		{
			if (rename(messageFn.c_str(), newName.c_str()) == 0)
			{
				index[msgNum].lastKnownFilename=s;
			}
			else
				changed=false;
		}

		messageFn=getfilename(msgNum);

		if (folderCallback)
			folderCallback->messageChanged(msgNum);
	}

	return changed;
}

string mail::maildir::getMaildirFlags(const mail::messageInfo &flags)
{
	string s="";

	if (flags.draft)
		s += "D";

	if (flags.replied)
		s += "R";

	if (flags.marked)
		s += "F";

	if (!flags.unread)
		s += "S";

	if (flags.deleted)
		s += "T";
	return s;
}

void mail::maildir::updateFolderIndexFlags(const vector<size_t> &messages,
					   bool doFlip,
					   bool enableDisable,
					   const mail::messageInfo &flags,
					   mail::callback &callback)
{
	string errmsg="Message updated";

	vector<size_t>::const_iterator b, e;

	b=messages.begin();
	e=messages.end();

	size_t n=index.size();

	MONITOR(mail::maildir);

	while (!DESTROYED() && b != e)
	{
		size_t i= *b++;

		if (i < n)
		{
#define DOFLAG(dummy, field, dummy2) \
			if (flags.field) \
			{ \
				index[i].field=\
					doFlip ? !index[i].field\
					       : enableDisable; \
			}

			LIBMAIL_MSGFLAGS;
#undef DOFLAG
		}

		if (!updateFlags(i))
			errmsg="Folder opened in read-only mode.";

	}

	callback.success(errmsg);
}

void mail::maildir::removeMessages(const vector<size_t> &messages,
				callback &cb)
{
	vector<size_t>::const_iterator b=messages.begin(), e=messages.end();

	while (b != e)
	{
		size_t n=*b++;

		string messageFn=getfilename(n);

		if (messageFn.size() > 0)
			unlink(messageFn.c_str());
	}
	updateFolderIndexInfo(&cb, false);
}

void mail::maildir::updateFolderIndexInfo(mail::callback &callback)
{
	updateFolderIndexInfo(&callback, true);
}

void mail::maildir::updateFolderIndexInfo(mail::callback *callback,
					  bool doExpunge)
{
	if (folderPath.size() == 0)
	{
		if (callback)
			callback->success("OK");
		return;
	}

	struct stat stat_buf;

	size_t n=index.size();

	mail::expungeList removedList;

	while (n > 0)
	{
		--n;

		string messageFn=getfilename(n);

		if (doExpunge)
		{
			if (!index[n].deleted && messageFn.size() > 0)
				continue;

			if (messageFn.size() > 0)
			{
				unlink(messageFn.c_str());
				messageFn="";
			}
		}

		if (messageFn.size() == 0 ||
		    stat(messageFn.c_str(), &stat_buf))
		{
			index.erase(index.begin() + n);

			removedList << n;
		}
	}

	removedList >> folderCallback;

	checkNewMail(callback);
}

void mail::maildir::copyMessagesTo(const vector<size_t> &messages,
				   mail::folder *copyTo,
				   mail::callback &callback)
{
	mail::copyMessages::copy(this, messages, copyTo, callback);
}

void mail::maildir::searchMessages(const mail::searchParams &searchInfo,
				   mail::searchCallback &callback)
{
	mail::searchMessages::search(callback, searchInfo, this);
}

// Open a new folder.

void mail::maildir::open(string pathStr, mail::callback &callback,
			 mail::callback::folder &folderCallbackArg)
{
	index.clear();
	updateNotify(false);

	folderCallback=nullptr;

	cacheEntity.reset();
	cacheFd.reset();

	if (path == "")
	{
		callback.fail(strerror(errno));
		return;
	}

	set<string> recentMessages;

	string md;

	char *d=maildir_name2dir(path.c_str(), pathStr.c_str());

	if (!d)
	{
		callback.fail(strerror(errno));
		return;
	}

	try {
		md=d;
		free(d);
	} catch (...) {
		free(d);
		LIBMAIL_THROW(LIBMAIL_THROW_EMPTY);
	}

	{
		struct stat stat_buf;

		string d=md + "/" KEYWORDDIR;
		/* New Courier-IMAP maildirwatch code */

		mkdir(d.c_str(), 0700);

		if (stat(md.c_str(), &stat_buf) == 0)
			chmod(d.c_str(), stat_buf.st_mode & 0777);
	}

	maildir_purgetmp(md.c_str());
	maildir_getnew(md.c_str(), nullptr,
		       &recent_callback_func, &recentMessages);

	scan(pathStr, index);

	folderCallback= &folderCallbackArg;
	folderPath=pathStr;

	vector<maildirMessageInfo>::iterator b=index.begin(), e=index.end();

	while (b != e)
	{
		b->recent=recentMessages.count(b->lastKnownFilename) > 0;
		b++;
	}

	lockFolder.reset(
		new ::maildir::watch{md}
	);

	readKeywordHelper rkh(this);

	bool rc;

	while (rkh.go(md, rc))
		;

	callback.success("Mail folder opened");
}

/*-------------------------------------------------------------------------*/

void mail::maildir::genericMessageRead(string uid,
				       size_t messageNumber,
				       bool peek,
				       mail::readMode readType,
				       mail::callback::message &callback)
{
	if (!fixMessageNumber(this, uid, messageNumber))
	{
		callback.success("OK");
		return;
	}

	MONITOR(mail::maildir);

	string messageFn=getfilename(messageNumber);

	if (messageFn.size() == 0)
	{
		callback.success("OK");
		return;
	}

	int fd=maildir_safeopen(messageFn.c_str(), O_RDONLY, 0);

	if (fd < 0)
	{
		callback.success("OK");
		return;
	}

	try {
		mail::file f(fd, "r");

		f.genericMessageRead(this, messageNumber, readType, callback);

		if (ferror(f))
		{
			callback.fail(strerror(errno));
			close(fd);
			return;
		}

	} catch (...) {
		close(fd);
		LIBMAIL_THROW(LIBMAIL_THROW_EMPTY);
	}

	close(fd);

	if (!peek && index[messageNumber].unread)
	{
		index[messageNumber].unread=false;
		saveFolderIndexInfo(messageNumber,
				    index[messageNumber],
				    callback);
		return;
	}

	callback.success("OK");
}

void mail::maildir::genericMessageSize(string uid,
				       size_t messageNumber,
				       mail::callback::message &callback)
{
	if (!fixMessageNumber(this, uid, messageNumber))
	{
		callback.messageSizeCallback(messageNumber, 0);
		callback.success("OK");
		return;
	}

	string messageFn=getfilename(messageNumber);

	if (messageFn.size() == 0)
	{
		callback.messageSizeCallback(messageNumber,0);
		callback.success("OK");
		return;
	}

	unsigned long ul;

	if (maildir_parsequota(messageFn.c_str(), &ul) == 0)
	{
		callback.messageSizeCallback(messageNumber, ul);
		callback.success("OK");
		return;
	}

	struct stat stat_buf;

	int rc=stat(messageFn.c_str(), &stat_buf);

	callback.messageSizeCallback(messageNumber,
				     rc == 0 ? stat_buf.st_size:0);
	callback.success("OK");
}

void mail::maildir::genericGetMessageFd(
	string uid,
	size_t messageNumber,
	bool peek,
	std::shared_ptr<rfc822::fdstreambuf> &fdRet,
	mail::callback &callback)
{
	std::shared_ptr<rfc2045::entity> dummy;

	genericGetMessageFdStruct(uid, messageNumber, peek, fdRet, dummy,
				  callback);
}

void mail::maildir::genericGetMessageStruct(
	string uid,
	size_t messageNumber,
	std::shared_ptr<rfc2045::entity> &structRet,
	mail::callback &callback)
{
	std::shared_ptr<rfc822::fdstreambuf> dummy;

	genericGetMessageFdStruct(uid, messageNumber, true, dummy, structRet,
				  callback);
}

bool mail::maildir::genericCachedUid(string uid)
{
	return uid == cacheUID && !cacheFd->error();
}

void mail::maildir::genericGetMessageFdStruct(
	string uid,
	size_t messageNumber,
	bool peek,
	std::shared_ptr<rfc822::fdstreambuf> &fdRet,
	std::shared_ptr<rfc2045::entity> &structRet,
	mail::callback &callback)
{
	if (uid == cacheUID && cacheFd && !cacheFd->error() && cacheEntity)
	{
		fdRet=cacheFd;
		structRet=cacheEntity;
		callback.success("OK");
		return;
	}

	if (!fixMessageNumber(this, uid, messageNumber))
	{
		callback.fail("Message removed on the server");
		return;
	}

	cacheFd.reset();
	cacheEntity.reset();

	string messageFn=getfilename(messageNumber);

	if (messageFn.size() == 0)
	{
		callback.fail("Message removed on the server");
		return;
	}

	int fd=maildir_safeopen(messageFn.c_str(), O_RDONLY, 0);

	cacheFd=std::make_shared<rfc822::fdstreambuf>(fd);

	if (cacheFd->error())
	{
		cacheFd.reset();
		callback.fail("Error opening the message");
		return;
	}

	cacheEntity=std::make_shared<rfc2045::entity>();

	if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0)
	{
		cacheFd.reset();
		cacheEntity.reset();
		callback.fail("Message removed on the server");
		return;
	}

	cacheUID="";

	{
		std::istreambuf_iterator<char> b{&*cacheFd}, e{};

		rfc2045::entity::line_iter<false>::iter parser{b, e};

		cacheEntity->parse(parser);
	}

	if (cacheFd->error())
	{
		cacheFd.reset();
		cacheEntity.reset();
		callback.fail(strerror(errno));
		return;
	}

	fdRet=cacheFd;
	structRet= cacheEntity;
	cacheUID=std::move(uid);

	if (!peek && index[messageNumber].unread)
	{
		index[messageNumber].unread=false;
		saveFolderIndexInfo(messageNumber, index[messageNumber],
				    callback);
		return;
	}
	callback.success("OK");
}

void mail::maildir::updateNotify(bool enableDisable,
				 mail::callback &callback)
{
	updateNotify(enableDisable);
	if (!enableDisable)
		updateFolderIndexInfo(&callback, false);
	else
		callback.success("OK");
}

void mail::maildir::updateNotify(bool enableDisable)
{
	if (!enableDisable)
	{
		watchFolderContents={};
		watchFolder={};
		return;
	}

	if (folderPath.size() == 0 || watchFolder)
		return;

	auto dir=::maildir::name2dir(path, folderPath);

	if (dir.empty())
		return;

	watchFolder.reset(new ::maildir::watch{dir});

	watchFolderContents.reset(new ::maildir::watch::contents{
			*watchFolder
		}
	);
	watchStarting=true;
}


void mail::maildir::getFolderKeywordInfo(size_t n, mail::keywords::list &s)
{
	string messageFn=getfilename(n);

	if (messageFn.size() == 0)
		return;

	s=index[n].keywords.keywords();
}

void mail::maildir::updateKeywords(const vector<size_t> &messages,
				   const mail::keywords::list &keywords,
				   bool setOrChange,
				   bool changeTo,
				   mail::callback &cb)
{
	if (folderPath.size() == 0)
	{
		cb.success("Ok.");
		return;
	}

	string dir= ::maildir::name2dir(path, folderPath);

	if (dir.empty())
	{
		cb.fail(strerror(errno));
		return;
	}

	unique_ptr<::maildir::watch::lock> imap_lock;

	if (lockFolder)
	{
		imap_lock.reset(
			new ::maildir::watch::lock{
				*lockFolder
			}
		);
	}

	MONITOR(mail::maildir);

	vector<size_t>::const_iterator b=messages.begin(),
		e=messages.end();

	while (!DESTROYED() && b != e)
	{
		size_t n= *b++;

		string messageFn=getfilename(n);

		if (messageFn.size() == 0)
			continue;

		mail::keywords::list cpy, orig=index[n].keywords.keywords();

		if (!setOrChange)
		{
			cpy=keywords;
		}
		else
		{
			cpy=orig;

			for (auto &kw:keywords)
			{
				if (changeTo)
					cpy.insert(kw);
				else
					cpy.erase(kw);
			}
		}

		if (cpy == orig)
			continue;

		int counter=0;

		while (!mail::keywords::update(
			       dir, index[n].lastKnownFilename, cpy)
		)
		{
			if (++counter > 1000)
				LIBMAIL_THROW("Unable to update keywords");

			mail::keywords::hashtable<string> h;
			unordered_map<string,
					   mail::keywords::message<string>
					   > messages;
			h.load(dir,
			       []
			       (const string &filename)
			       {
				       return filename;
			       },
			       [&]
			       (const string &filename,
				mail::keywords::list &keywords)
			       {
				       messages[filename].keywords(
					       h,
					       keywords,
					       filename
				       );
				       return true;
			       },
			       []
			       {
				       return false;
			       });
		}
	}

	cb.success("Message keywords updated.");
}
