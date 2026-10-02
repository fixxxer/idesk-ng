/* vim:tabstop=4:expandtab:shiftwidth=4
 * 
 * Idesk -- App.h
 *
 * Copyright (c) 2002, Chris (nikon) (nikon@sc.rr.com)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 *      Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *      
 *      Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *      
 *      Neither the name of the <ORGANIZATION> nor the names of its
 *      contributors may be used to endorse or promote products derived from
 *      this software without specific prior written permission.
 *
 * (See the included file COPYING / BSD )
 */

#include "App.h"
#include "Migrate.h"
#include <signal.h>
/*#include <sys/wait.h>*/

Application::Application(int arg, char ** args) : AbstractApp(arg, args)
{

    if (!processArguments())
        _exit(1);

    startIdesk();
}

Application::~Application()
{
    delete container;
}

bool Application::processArguments()
{
    bool returnBool = true;
    string tmpStr;
    
    for (int i = 0; i < argc; i++)
    {
        tmpStr = argv[i];
        if (tmpStr == "-h" || tmpStr == "-v" || tmpStr == "--help" || tmpStr == "--usage")
        {
            cerr << "Idesk " << VERSION << endl << "==============\n" 
                 << "Default Prefix " << DEFAULT_PREFIX << endl << "==============\n" 
                 << "Further documentation available at:"
                 << " http://idesk.sourceforge.net\n"
                 << "\nRemember to create your ~/.config/idesktop/ideskrc file,"
                 << " and put .lnk icons in the ~/.config/idesktop\ndirectory.\n"
                 << "\niDesk-NG: --migrate-to-desktop converts .lnk icons to"
                 << " .desktop (see DESIGN.md).\n";
            returnBool = false;
        }
        else if (tmpStr == "--migrate-to-desktop")
        {
            // one-shot CLI utility, not a flag that changes the normal
            // startup path: runs the migration and exits immediately,
            // same as --help does, rather than falling through to
            // startIdesk()
            _exit(runMigration() ? 0 : 1);
        }
    }
    return returnBool;
}

void signalhandler(int sig){
	if(sig == SIGCHLD){
	  int status;
	  waitpid(-1, &status, WNOHANG|WUNTRACED);
	}else{
	  _exit(1); 	
	}
}

void Application::startIdesk()
{
    // setup signals
    signal(SIGSEGV, signalhandler);
    signal(SIGFPE, signalhandler);
    signal(SIGTERM, signalhandler);
    signal(SIGINT, signalhandler);
    signal(SIGUSR1, signalhandler);
    signal(SIGUSR2, signalhandler);
    signal(SIGHUP, signalhandler);
    signal(SIGCHLD, signalhandler);
    
    container = new XDesktopContainer(this);
    if (!container)
                cerr << "container is NULL\n";
    container->run();
}

void Application::restartIdesk()
{
    container->saveState();
    cerr << "restarting idesk\n";
    delete container;
    
    
    //startIdesk();
    //container->create();
    //container->run();

    execvp( argv[0], argv );
}
