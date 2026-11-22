package genesis;

public class SpriteLine {
	
	private static final int PRIORITY_MASK = 	0x80000000;
	private static final int PALETTE_MASK = 	0x60000000;
	private static final int HORIZONTAL_FLIP =	0x08000000;
	private static final int WIDTH_MASK = 		0x06000000;
	private static final int HEIGHT_MASK = 		0x01800000;
	private static final int ADDR_MASK = 		0x007FFE00; 		
	private static final int X_MASK = 			0x000001FF;

	/**
	 * pcc_ fwwh haaa aaaa aaaa aaax xxxx xxxx
	 * 
	 * f: horizontal flip
	 * p: priority
	 * c: palette number
	 * h: height
	 * w: width
	 * x: xPosition
	 * a: address
	 */
	private int data;
	
	public SpriteLine() {
		super();
	}
	
	public SpriteLine(int data) {
		this.data = data;
	}
	
	public int getPriority() {
		return (data & PRIORITY_MASK) >>> 31;
	}

	public void setPriority(int priority) {
		data &= ~PRIORITY_MASK;
		data |= ((priority & 1) << 31);
	}

	public int getPaletteNumber() {
		return (data & PALETTE_MASK) >> 29;
	}

	public void setPaletteNumber(int paletteNumber) {
		data &= ~PALETTE_MASK;
		data |= ((paletteNumber & 3) << 29);
	}

	public int getHorizontalFlip() {
		return (data & HORIZONTAL_FLIP) >> 27;
	}

	public void setHorizontalFlip(int horizontalFlip) {
		data &= ~HORIZONTAL_FLIP;
		data |= ((horizontalFlip & 1) << 27);
	}
	
	public int getWidth() {
		return (data & WIDTH_MASK) >> 25;
	}

	public void setWidth(int width) {
		data &= ~WIDTH_MASK;
		data |= ((width & 3) << 25);
	}

	public int getHeight() {
		return (data & HEIGHT_MASK) >> 23;
	}

	public void setHeight(int height) {
		data &= ~HEIGHT_MASK;
		data |= ((height & 3) << 23);
	}

	public int getAddr() {
		return (data & ADDR_MASK) >> 9;
	}

	public void setAddr(int addr) {
		data &= ~ADDR_MASK;
		data |= ((addr & 16_383) << 9);
	}

	public int getX() {
		return data & X_MASK;
	}

	public void setX(int x) {
		data &= ~X_MASK;
		data |= (x & 511);
	}
	
	public byte toPixelData() {
		return (byte) ((data & (PRIORITY_MASK | PALETTE_MASK)) >>> 25); 
	}

}
