use bitcoin::{
    consensus::{Decodable, Encodable},
    io::{self, ErrorKind},
};

#[derive(Debug, PartialEq, Eq)]
pub struct CompactVec<T>(Vec<T>);

impl<T: Encodable + std::fmt::Debug> Encodable for CompactVec<T> {
    fn consensus_encode<W: bitcoin::io::Write + ?Sized>(
        &self,
        w: &mut W,
    ) -> Result<usize, bitcoin::io::Error> {
        let mut len = VarInt(self.0.len() as u64).consensus_encode(w)?;
        for item in self.0.iter() {
            len += item.consensus_encode(w)?;
        }
        Ok(len)
    }
}

impl<T: Decodable + std::fmt::Debug> Decodable for CompactVec<T> {
    fn consensus_decode<R: bitcoin::io::Read + ?Sized>(
        r: &mut R,
    ) -> Result<Self, bitcoin::consensus::encode::Error> {
        let len = VarInt::consensus_decode(r)?.0;
        let mut ret = Vec::with_capacity(len as usize);
        for _ in 0..len {
            ret.push(Decodable::consensus_decode(r)?);
        }
        Ok(CompactVec(ret))
    }
}

impl<T> From<Vec<T>> for CompactVec<T> {
    fn from(v: Vec<T>) -> Self {
        Self(v)
    }
}

impl<T> AsRef<Vec<T>> for CompactVec<T> {
    fn as_ref(&self) -> &Vec<T> {
        &self.0
    }
}

#[derive(Debug, PartialEq, Eq)]
pub struct Maybe<T>(pub Option<T>);
impl<T: Encodable + std::fmt::Debug> Encodable for Maybe<T> {
    fn consensus_encode<W: bitcoin::io::Write + ?Sized>(
        &self,
        w: &mut W,
    ) -> Result<usize, bitcoin::io::Error> {
        match &self.0 {
            Some(v) => v.consensus_encode(w),
            None => Ok(0),
        }
    }
}

impl<T: Decodable + std::fmt::Debug> Decodable for Maybe<T> {
    fn consensus_decode<R: bitcoin::io::Read + ?Sized>(
        r: &mut R,
    ) -> Result<Self, bitcoin::consensus::encode::Error> {
        match T::consensus_decode(r) {
            Ok(v) => Ok(Self(Some(v))),
            Err(bitcoin::consensus::encode::Error::Io(e))
                if e.kind() == ErrorKind::UnexpectedEof =>
            {
                Ok(Self(None))
            }
            Err(e) => Err(e),
        }
    }
}

impl<T> From<Option<T>> for Maybe<T> {
    fn from(v: Option<T>) -> Self {
        Self(v)
    }
}

#[derive(Debug, PartialEq, Eq)]
pub struct RawBytes(pub Vec<u8>);

impl Encodable for RawBytes {
    fn consensus_encode<W: bitcoin::io::Write + ?Sized>(
        &self,
        writer: &mut W,
    ) -> Result<usize, bitcoin::io::Error> {
        writer.write(&self.0)
    }
}

impl Decodable for RawBytes {
    fn consensus_decode<R: bitcoin::io::Read + ?Sized>(
        reader: &mut R,
    ) -> Result<Self, bitcoin::consensus::encode::Error> {
        let mut buf = [0u8; 512];
        let v = reader.read(&mut buf)?;
        Ok(Self(buf[..v].to_vec()))
    }
}

/// This `VarInt` struct is designed to encode/decode in-line with Bitcoin core VarInt implementation
///
/// ## Motivation
/// In the rust-bitcoin library, variable-length integers are implemented as CompactSize.
/// See [issue #1016](https://github.com/rust-bitcoin/rust-bitcoin/issues/1016)

#[derive(Debug, PartialEq, Eq)]
pub struct VarInt(pub u64);

impl Encodable for VarInt {
    fn consensus_encode<W: io::Write + ?Sized>(&self, writer: &mut W) -> Result<usize, io::Error> {
        let mut n = self.0;
        let mut len = 0;
        let mut tmp = Vec::new();

        loop {
            let byte = ((n & 0x7F) | if len > 0 { 0x80 } else { 0x00 }) as u8;
            tmp.push(byte);
            len += 1;

            if n <= 0x7F {
                break;
            }
            n = (n >> 7) - 1;
        }

        for byte in tmp.iter().rev() {
            writer.write_all(&[*byte])?;
        }

        Ok(len)
    }
}

impl Decodable for VarInt {
    fn consensus_decode<R: io::Read + ?Sized>(
        reader: &mut R,
    ) -> Result<Self, bitcoin::consensus::encode::Error> {
        let mut n = 0u64;

        loop {
            let mut buf = [0u8; 1];
            reader.read_exact(&mut buf)?;
            let ch_data = buf[0];
            if n > (u64::MAX >> 7) {
                return Err(bitcoin::consensus::encode::Error::Io(io::Error::new(
                    io::ErrorKind::InvalidInput,
                    "VarInt: size too large",
                )));
            }
            n = (n << 7) | (ch_data & 0x7F) as u64;
            if ch_data & 0x80 != 0 {
                if n == u64::MAX {
                    return Err(bitcoin::consensus::encode::Error::Io(io::Error::new(
                        io::ErrorKind::InvalidInput,
                        "VarInt: size too large",
                    )));
                }
                n += 1;
            } else {
                break;
            }
        }

        Ok(VarInt(n))
    }
}
