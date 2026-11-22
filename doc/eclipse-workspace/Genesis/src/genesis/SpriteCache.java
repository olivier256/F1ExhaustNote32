package genesis;

public class SpriteCache {
	
	private static final int Y_MASK = 		0x01FF0000;
	private static final int WIDTH_MASK = 	0x00000C00;
	private static final int HEIGHT_MASK = 	0x00000300;
	private static final int LINK_MASK =	0x0000007F;
	
	/**
	 * ---- ---y yyyy yyyy ---- wwhh -lll llll
	 */
	private int data;
	
	public int getY() {
		return (data & Y_MASK) >> 16;
	}
	
	public void setY(int y) {
		int _y = (y & 511) << 16;
		data &= ~Y_MASK;
		data |= _y;
	}
	
	public int getWidth() {
		return (data & WIDTH_MASK) >> 10;
	}

	public void setWidth(int width) {
		int _width = (width & 3) << 10;
		data &= ~WIDTH_MASK;
		data |= _width;
	}

	public int getHeight() {
		return (data & HEIGHT_MASK) >> 8;
	}

	public void setHeight(int height) {
		int _height = (height & 3) << 8;
		data &= ~HEIGHT_MASK;
		this.data |= _height;
	}
	
	public int getLinkNumber() {
		return data & LINK_MASK;
	}

	public void setLinkNumber(int linkNumber) {
		int _linkNumber = linkNumber & 127;
		data &= ~LINK_MASK;
		data |= _linkNumber;
	}
	


}
