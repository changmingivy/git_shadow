bool push(git_repository *repository) 
 {
     // get the remote.
     git_remote* remote = NULL;
     git_remote_lookup( &remote, repository, "origin" );

     // connect to remote
     git_remote_connect( remote, GIT_DIRECTION_PUSH )

     // add a push refspec
     git_remote_add_push( remote, "refs/heads/master:refs/heads/master" );
     // configure options
     git_push_options options;
     git_push_init_options( &options, GIT_PUSH_OPTIONS_VERSION );

     // do the push
     git_remote_upload( remote, NULL, &options );
     return true; 
 }
