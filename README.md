#zshadow    
##基于 git/libgit2 的并发 [布署 + 监控] 系统    
=============================================    
    
    
##交互格式更改    
####旧格式    
######[{"meta":,"data0":,...,"dataN":},{"meta":,"data0":,...,"dataN":},{"meta":,"data0":,...,"dataN":}]    
####新格式    
######{"meta":,"data":[{"data0":,...,"dataN":},{"data0":,...,"dataN":},{"data0":,...,"dataN":}]}    
     
    
##字段名称更改    
####1、新建项目    
######不再使用 data 与 ExtraData 两个笼统的字段，改用语义更加明确的具栖字段名称，示例如下：    
######{"ProjId":"5","PathOnHost":"/home/git/myproj","SourceUrl":"http://.../test.git","SourceBranch":"master","SourceVcsType":"git","NeedPull":"Y"}    
    
    
####2、错误码    
######服务端返回的错误码改为放置在 ErrNo 字段中，不在复用 OpsId 字段    
