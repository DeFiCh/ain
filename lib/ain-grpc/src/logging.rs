use std::io::Write;

#[derive(Debug, Default)]
pub struct CppLogTarget {
    enable_stdout: bool,
}

pub fn cpp_log_target_format<'a>(
    buf: &mut env_logger::fmt::Formatter,
    record: &log::Record<'a>,
) -> std::io::Result<()> {
    let mod_path = record.module_path();
    if let Some(mod_path) = mod_path {
        writeln!(buf, "[{}] {}", mod_path, record.args())
    } else {
        writeln!(buf, "{}", record.args())
    }
}

impl CppLogTarget {
    pub fn new(enable_stdout: bool) -> Self {
        Self { enable_stdout }
    }

    fn write_buf(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        if self.enable_stdout {
            let mut out = std::io::stdout().lock();
            _ = out.write(buf)?;
        }
        let s = std::str::from_utf8(buf).or(Err(std::io::Error::new(
            std::io::ErrorKind::Other,
            "Invalid UTF-8 sequence",
        )))?;
        ain_cpp_imports::log_print(s);
        Ok(buf.len())
    }
}

impl std::io::Write for CppLogTarget {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        self.write_buf(buf)
    }

    fn flush(&mut self) -> std::io::Result<()> {
        Ok(())
    }
}
