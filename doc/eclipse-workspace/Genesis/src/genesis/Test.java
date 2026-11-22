package genesis;

public class Test {
	
	public Test() {
		boolean assertenabled = false;
		assert assertenabled = true;
		if (!assertenabled) {
			throw new AssertionError("Lancer avec l'argument VM '-ea'");
		}
	}
	
}
