//! Per-player byte encoding. Script and save bytes are preserved verbatim.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub enum TextEncoding {
    #[default]
    ShiftJis,
    Gbk,
}
impl TextEncoding {
    pub(crate) fn no_break(self, code: u16) -> bool {
        match self {
            Self::ShiftJis => [0x8141, 0x8142, 0x8176, 0x8178].contains(&code),
            Self::Gbk => [0xa1a2, 0xa1a3, 0xa1b9, 0xa1bb].contains(&code),
        }
    }
    pub(crate) fn voiced_mark(self, code: u16) -> bool {
        code == match self {
            Self::ShiftJis => 0x814a,
            Self::Gbk => 0xa961,
        }
    }
    pub fn is_lead(self, byte: u8) -> bool {
        match self {
            Self::ShiftJis => (0x81..=0x9f).contains(&byte) || (0xe0..=0xfc).contains(&byte),
            Self::Gbk => (0x81..=0xfe).contains(&byte),
        }
    }
}
