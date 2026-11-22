package genesis;

public class Sprite {

	private static final short Y_MASK = 				0x01FF;
	private static final short WIDTH_MASK = 			0x0C00;
	private static final short HEIGHT_MASK = 			0x0300;
	private static final short LINK_MASK =	 			0x007F;
	private static final int PRIORITY_MASK =		0x00008000;
	private static final short PALETTE_MASK = 			0x6000;
	private static final short VERTICAL_FLIP_MASK =		0x1000;
	private static final short HORIZONTAL_FLIP_MASK = 	0x0800;
	private static final short TILE_NUMBER_MASK =		0x07FF;
	private static final short X_MASK = 				0x01FF;
	
	/**
	 * 0:	-------y yyyyyyyy
	 * 2:	----wwhh -lllllll
	 * 4:	pccvhnnn nnnnnnnn
	 * 6:	-------x xxxxxxxx
	 * 
	 */
	private short data0;
	private short data1;
	private short data2;
	private short data3;
	
	public int getY() {
		return data0 & Y_MASK;
	}
	
	public void setY(int y) {
		data0 &= ~Y_MASK;
		data0 |= (y & 511);
	}
	
	public int getWidth() {
		return (data1 & WIDTH_MASK) >> 10;
	}

	public void setWidth(int width) {
		data1 &= ~WIDTH_MASK;
		data1 |= ((width & 3) << 10);
	}

	public int getHeight() {
		return (data1 & HEIGHT_MASK) >> 8;
	}

	public void setHeight(int height) {
		data1 &= ~HEIGHT_MASK;
		data1 |= ((height & 3) << 8);
	}
	
	public int getLinkNumber() {
		return data1 & LINK_MASK;
	}

	public void setLinkNumber(int linkNumber) {
		data1 &= ~LINK_MASK;
		data1 |= (linkNumber & 127);
	}
	
	public int getPriority() {
		return (data2 & PRIORITY_MASK) >> 15;
	}

	public void setPriority(int priority) {
		data2 &= ~PRIORITY_MASK;
		data2 |= ((priority & 1) << 15);
	}
	
	public int getPaletteNumber() {
		return (data2 & PALETTE_MASK) >> 13;
	}

	public void setPaletteNumber(int paletteNumber) {
		data2 &= ~PALETTE_MASK;
		data2 |= ((paletteNumber & 3) << 13);
	}
	
	public int getVerticalFlip() {
		return (data2 & VERTICAL_FLIP_MASK) >> 12;
	}
	
	public void setVerticalFlip(int verticalFlip) {
		data2 &= ~VERTICAL_FLIP_MASK;
		data2 |= (verticalFlip & 1) << 12;
	}
	
	public int getHorizontalFlip() {
		return (data2 & HORIZONTAL_FLIP_MASK) >> 11;
	}

	public void setHorizontalFlip(int horizontalFlip) {
		data2 &= ~HORIZONTAL_FLIP_MASK;
		data2 |= ((horizontalFlip & 1) << 11);
	}
	
	public int getTileNumber() {
		return data2 & TILE_NUMBER_MASK;
	}

	public void setTileNumber(int tileNumber) {
		data2 &= ~TILE_NUMBER_MASK;
		data2 |= (tileNumber & 2047);
	}
	
	public int getX() {
		return data3 & X_MASK;
	}

	public void setX(int x) {
		data3 &= ~X_MASK;
		data3 |= (x & 511);
	}
	
	public int toSpriteLine() {
		int m1 = PRIORITY_MASK | PALETTE_MASK | HORIZONTAL_FLIP_MASK;
		int m2 = WIDTH_MASK | HEIGHT_MASK;
		int upperWord = (data2 & m1) | (data1 & m2) >> 1;
		return  (upperWord << 16) | data3 ;
	}

	@Override
	public String toString() {
		short word0 = (short) (data0 & Y_MASK);
		short word1 = (short) (data1 & (WIDTH_MASK | HEIGHT_MASK | LINK_MASK));
		long word2Mask = PRIORITY_MASK | PALETTE_MASK | VERTICAL_FLIP_MASK | HORIZONTAL_FLIP_MASK | TILE_NUMBER_MASK;
		short word2 = (short) (data2 & word2Mask);
		short word3 = (short) (data3 & X_MASK);
		return "\tDC.W\t$" + String.format("%04X", word0) + "," //
				+ "$" + String.format("%04X", word1) + "," //
				+ "$" + String.format("%04X", word2) + "," //
				+ "$" + String.format("%04X", word3);
	}


}
