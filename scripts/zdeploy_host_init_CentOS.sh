#!/bin/sh
zInitEnv() {
    zProjName="miaopai"
    zDeployPath=/home/git/$zProjName

    rm -rf /home/git/*
    cp -rp ../demo/${zProjName}_shadow /home/git/
    mkdir $zDeployPath
    ln -sv /home/git/${zProjName}_shadow $zDeployPath/.git_shadow

    #Init Deploy Git Env
    cd $zDeployPath
    git init .
    git commit --allow-empty -m "__deploy_init__"
    git branch CURRENT
    git branch server  #Act as Git server

    printf "#!/bin/sh
        export GIT_DIR=\"%zCodePath/.git\"
        export PATH=\"/bin:/sbin:/usr/bin:/usr/sbin:/usr/local/bin:/usr/local/sbin\"

        cd $zDeployPath &&
        rm -f .git_shadow &&

        git pull --force ./.git server:master &&

        ln -sv /home/git/${zProjName}_shadow ${zDeployPath}/.git_shadow
		" > $zDeployPath/.git/hooks/post-update

    chmod 0555 $zDeployPath/.git/hooks/post-update
}


useradd -m -G wheel -s /bin/sh git
zInitEnv
chown -R git:git /home/git

# 在 /tmp 路径下初始化一个客户端角色，方便测试
rm -rf /tmp/miaopai_client
mkdir /tmp/miaopai_client
cd /tmp/miaopai_client
svn co svn://$1:50000
cp /etc/* ./ 2>/dev/null
svn add *
svn commit -m "etc files"
svn up
