use std::env;
use std::process;
use std::io::prelude::*;
use std::net::TcpStream;
use std::time::Duration;


fn main() {
    let args: Vec<String> = env::args().collect();

    let mut port: &str = "20000";
    let content: &str;

    if 3 == args.len() {
        content = &args[2];
    } else if 4 == args.len() {
        port = &args[2];
        content = &args[3];
    } else {
        println! ("\n\nUsage:\n\tnotice <serv_ip> [serv_port, default 20000] <content_to_send>");
        process::exit(1);
    }

    let mut addr = args[1].clone();
    addr.push_str(":");
    addr.push_str(port);

    let mut recv_buf = vec![];

    if let Ok(mut stream) = TcpStream::connect(addr) {
        match stream.set_read_timeout(Some(Duration::from_secs(3))) {
            Err(error) => {
				println! ("set_read_timeout: {}!", error);
			},
            _ => (),
        }

        stream.write(content.as_bytes()).expect("send err!");
        stream.read(&mut recv_buf).expect("recv err!");

        println! ("received {:?}", recv_buf);
    } else {
        println! ("connect err!");
    }
}
