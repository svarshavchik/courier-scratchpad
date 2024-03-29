/*
** Copyright 2003, Double Precision Inc.
**
** See COPYING for distribution information.
*/
#ifndef libmail_smapopen_H
#define libmail_smapopen_H

#include "libmail_config.h"
#include	"mail.H"
#include	"imap.H"
#include	"smap.H"

#include	<vector>

LIBMAIL_START

class smapFOLDER;

class smapOPEN : public smapHandler {

	std::string path;
	mail::snapshot *restoreSnapshot;
	mail::callback::folder &folderCallback;

	smapFOLDER *newSmapFolder;

	const char *getName() override;

	size_t exists;

	bool restoringSnapshot;
	size_t presnapshotExists;

	class SnapshotRestoreHelper;

public:
	smapOPEN(std::string pathArg,
		 mail::snapshot *restoreSnapshotArg,
		 mail::callback &openCallbackArg,
		 mail::callback::folder &folderCallbackArg);

	~smapOPEN();
	void installed(imap &) override;
	bool processLine(imap &imapAccount,
			 std::vector<const char *> &words) override;
	bool ok(std::string) override;

	void messagesRemoved(std::vector< std::pair<size_t, size_t> >
			     &removedList) override;
	void messageChanged(size_t msgNum) override;
};

LIBMAIL_END

#endif
