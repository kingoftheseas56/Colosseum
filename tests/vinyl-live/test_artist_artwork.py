import importlib.util
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('artwork_provider', ROOT/'scripts/vinyl/provider.py')
provider = importlib.util.module_from_spec(spec)
spec.loader.exec_module(provider)
MBID = 'b95ce3ff-3d05-4e87-9e01-c97b66af13d4'

class ArtworkContract(unittest.TestCase):
    def fetch(self, artists):
        return provider.fetch_artist_artwork(MBID, get_json=lambda url: {'artists':artists})
    def test_four_fanart_slots_preserve_identity_and_provenance(self):
        artist={'idArtist':'123','strMusicBrainzID':MBID,'strArtist':'Eminem'}
        for i in range(4):
            artist['strArtistFanart'+(str(i+1) if i else '')]='https://r2.theaudiodb.com/images/media/artist/fanart/photo%d.jpg'%i
        artist['strAlbumThumb']='https://example.com/album.jpg'
        result=self.fetch([artist])
        self.assertEqual(len(result['photos']),4)
        self.assertEqual(result['artistMbid'],MBID)
        self.assertEqual(result['usage'],'development')
        self.assertEqual(result['source'],'theaudiodb')
    def test_wrong_identity_and_duplicates_cannot_fill_four(self):
        artist={'strMusicBrainzID':MBID,'strArtistFanart':'https://r2.theaudiodb.com/images/media/artist/fanart/a.jpg'}
        artist['strArtistFanart2']=artist['strArtistFanart']
        self.assertEqual(len(self.fetch([artist])['photos']),1)
        artist['strMusicBrainzID']='11111111-1111-4111-8111-111111111111'
        self.assertEqual(self.fetch([artist])['photos'],[])
    def test_empty_and_untrusted_urls_are_intentional_empty_sets(self):
        self.assertEqual(self.fetch([])['photos'],[])
        self.assertEqual(self.fetch([{'strMusicBrainzID':MBID,'strArtistFanart':'http://localhost/a.jpg'}])['photos'],[])
    def test_invalid_identity_does_not_start_network(self):
        with self.assertRaises(provider.InvalidArgumentError):
            provider.fetch_artist_artwork('not-an-mbid',get_json=lambda url:self.fail('network'))

if __name__=='__main__':unittest.main()
