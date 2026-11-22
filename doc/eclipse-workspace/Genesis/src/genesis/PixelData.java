package genesis;

public class PixelData {
	private static final byte PRIORITY_MASK = 0x40;
	private static final byte PALETTE_MASK = 0x30;
	private static final byte PIXEL_MASK = 0x0F;
	
	/**
	 * -pcc tttt
	 * 
	 * p: priority
	 * c: palette number
	 * t: tile pixel color
	 */
	private byte data;
	
	public PixelData() {
		super();
	}
	
	public PixelData(byte data) {
		this.data = data;
	}
	
	public int getPriority() {
		return (data & PRIORITY_MASK) >> 6;
	}
	
	public void setPriority(int priority) {
		data &= ~PRIORITY_MASK;
		data |= ((priority & 1) << 6);
	}
	
	public int getPaletteNumber() {
		return (data & PALETTE_MASK) >> 4;
	}
	
	public void setPaletteNumber(int paletteNumber) {
		data &= ~PALETTE_MASK;
		data |= ((paletteNumber & 3) << 4);
	}
	
	public int getPixel() {
		return data & PIXEL_MASK;
	}
	
	public void setPixel(int pixel) {
		data &= ~PIXEL_MASK;
		data |= (pixel & 15);
	}
	
	public int getCramAddress() {
		return data & (PALETTE_MASK | PIXEL_MASK);
	}

}
