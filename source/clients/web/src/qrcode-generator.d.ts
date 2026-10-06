declare module 'qrcode-generator' {
  interface QrCode {
    addData(data: string, mode?: 'Numeric' | 'Alphanumeric' | 'Byte' | 'Kanji'): void
    make(): void
    createDataURL(cellSize?: number, margin?: number): string
  }

  export default function qrcode(
    typeNumber?: number,
    errorCorrectionLevel?: 'L' | 'M' | 'Q' | 'H',
  ): QrCode
}
