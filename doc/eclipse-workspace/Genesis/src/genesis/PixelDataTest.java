package genesis;

public class PixelDataTest extends Test {
	
	public void testPriority() {
		PixelData pixelData = new PixelData();
		int expected = 0;
		pixelData.setPriority(expected);
		int actual = pixelData.getPriority();
		assert actual == expected : "testPriority(). Expected: " + expected + " - Actual: " + actual;
		expected = 1;
		pixelData.setPriority(expected);
		actual = pixelData.getPriority();
		assert actual == expected : "testPriority(). Expected: " + expected + " - Actual: " + actual;
	}
	
	public void testPaletteNumber() {
		PixelData pixelData = new PixelData();
		for (int paletteNumber = 0; paletteNumber < 4; paletteNumber++) {
			pixelData.setPaletteNumber(paletteNumber);
			int actual = pixelData.getPaletteNumber();
			assert actual == paletteNumber : "testPaletteNumber(). Expected: " + paletteNumber + " - Actual: " + actual;
		}
	}
	
	public void testPixel() {
		PixelData pixelData = new PixelData();
		for (int pixel = 0; pixel < 16; pixel++) {
			pixelData.setPixel(pixel);
			int actual = pixelData.getPixel();
			assert actual == pixel : "testPixel(). Expected: " + pixel + " - Actual: " + actual;
		}
	}
	
	private void testCramAddress() {
		PixelData pixelData = new PixelData();
			for (int paletteNumber = 0; paletteNumber < 4; paletteNumber++) {
				for (int pixel = 0; pixel < 16; pixel++) {
					pixelData.setPaletteNumber(paletteNumber);
					pixelData.setPixel(pixel);
					int expected = paletteNumber << 4 | pixel;
					int cramAddress = pixelData.getCramAddress();
					assert expected == cramAddress : "testCramAddress(). Expected: " + expected + " - Actual: " + cramAddress;
				}
			}
	}

	public static void main(String[] args) {
		PixelDataTest test = new PixelDataTest();
		
		test.testPriority();
		test.testPaletteNumber();
		test.testPixel();
		test.testCramAddress();
		
		System.out.println(("Tests terminés avec succès"));
	}

}
