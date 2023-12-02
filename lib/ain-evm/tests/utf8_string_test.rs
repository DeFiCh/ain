#[cfg(test)]
use std::str;

#[test]
fn check_for_valid_utf8_strings() {
    let test1 = "abcdefghijklmnopqrstuvwxyz1234567890~_= ^+%]{}";
    let test2 = "abcdeàèéìòù";
    let test3 = "😁 Beaming Face With Smiling Eyes";
    let test4 = "Slightly Smiling Face 🙂";
    let test5 = "🤣🤣🤣 Rolling on the Floor Laughing";
    let test6 = "🤩🤩🤩 Star-🤩Struck 🤩🤩";
    let test7 = "Left till here away at to whom past. Feelings laughing at no wondered repeated provided finished. \
        It acceptance thoroughly my advantages everything as. Are projecting inquietude affronting preference saw who. \
        Marry of am do avoid ample as. Old disposal followed she ignorant desirous two has. Called played entire roused \
        though for one too. He into walk roof made tall cold he. Feelings way likewise addition wandered contempt bed \
        indulged.";

    let test1_bytes = test1.as_bytes();
    let test2_bytes = test2.as_bytes();
    let test3_bytes = test3.as_bytes();
    let test4_bytes = test4.as_bytes();
    let test5_bytes = test5.as_bytes();
    let test6_bytes = test6.as_bytes();
    let test7_bytes = test7.as_bytes();

    assert_eq!(str::from_utf8(test1_bytes), Ok(test1));
    assert_eq!(str::from_utf8(test2_bytes), Ok(test2));
    assert_eq!(str::from_utf8(test3_bytes), Ok(test3));
    assert_eq!(str::from_utf8(test4_bytes), Ok(test4));
    assert_eq!(str::from_utf8(test5_bytes), Ok(test5));
    assert_eq!(str::from_utf8(test6_bytes), Ok(test6));
    assert_eq!(str::from_utf8(test7_bytes), Ok(test7));
}

#[test]
fn check_for_invalid_utf8_strings() {
    let smiling_face = "😁".as_bytes();
    let laughing_face = "🤣".as_bytes();
    let star_struck_face = "🤩".as_bytes();

    let mut test1 = smiling_face[0..1].to_vec();
    test1.extend_from_slice(" Beaming Face With Smiling Eyes".as_bytes());

    let mut test2 = laughing_face[0..3].to_vec();
    test2.extend_from_slice(&laughing_face[0..2]);
    test2.extend_from_slice(&laughing_face[0..1]);
    test2.extend_from_slice(" Rolling on the Floor Laughing".as_bytes());

    let mut test3 = star_struck_face[0..1].to_vec();
    test3.extend_from_slice("🤩🤩 Star-🤩Struck 🤩🤩".as_bytes());

    assert!(str::from_utf8(test1.as_slice()).is_err());
    assert!(str::from_utf8(test2.as_slice()).is_err());
    assert!(str::from_utf8(test3.as_slice()).is_err());
}
